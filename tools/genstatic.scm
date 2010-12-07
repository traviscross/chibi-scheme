#! /usr/bin/env chibi-scheme

(import (chibi filesystem)
        (chibi pathname))

(define c-libs '())

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (x->string x)
  (cond ((string? x) x)
        ((symbol? x) (symbol->string x))
        ((number? x) (number->string x))
        (else (error "non-stringable object" x))))

(define (string-scan c str . o)
  (let ((limit (string-length str)))
    (let lp ((i (if (pair? o) (car o) 0)))
      (cond ((>= i limit) #f)
            ((eqv? c (string-ref str i)) i)
            (else (lp (+ i 1)))))))

(define (string-replace str c r)
  (let ((len (string-length str)))
    (let lp ((from 0) (i 0) (res '()))
      (define (collect) (if (= i from) res (cons (substring str from i) res)))
      (cond
       ((>= i len) (string-concatenate (reverse (collect))))
       ((eqv? c (string-ref str i)) (lp (+ i 1) (+ i 1) (cons r (collect))))
       (else (lp from (+ i 1) res))))))

(define (c-char? c)
  (or (char-alphabetic? c) (char-numeric? c) (memv c '(#\_ #\- #\! #\?))))

(define (c-escape str)
  (let ((len (string-length str)))
    (let lp ((from 0) (i 0) (res '()))
      (define (collect) (if (= i from) res (cons (substring str from i) res)))
      (cond
       ((>= i len) (string-concatenate (reverse (collect))))
       ((not (c-char? (string-ref str i))) (lp (+ i 1) (+ i 1) (cons "_" (cons (number->string (char->integer (string-ref str i)) 16) (collect)))))
       (else (lp from (+ i 1) res))))))

(define (mangle x)
  (string-replace
   (string-replace (string-replace (c-escape (x->string x)) #\- "_") #\? "_p")
   #\! "_x"))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (path-relative path dir)
  (let ((p-len (string-length path))
        (d-len (string-length dir)))
    (and (> p-len d-len)
         (string=? dir (substring path 0 d-len))
         (cond
          ((eqv? #\/ (string-ref path d-len))
           (substring path (+ d-len 1) p-len))
          ((eqv? #\/ (string-ref path (- d-len 1)))
           (substring path d-len p-len))
          (else #f)))))

(define (path-split file)
  (let ((len (string-length file)))
    (let lp ((i 0) (res '()))
      (let ((j (string-scan #\/ file i)))
        (cond
         (j (lp (+ j 1) (cons (substring file i j) res)))
         (else (reverse (if (= i len)
                            res
                            (cons (substring file i len) res)))))))))

(define (init-name mod)
  (string-append "sexp_init_lib_"
                 (string-concatenate (map mangle mod) "_")))

(define (find-c-libs basedir)
  (define (process-dir dir)
    (directory-fold
     dir
     (lambda (f x)
       (if (and (not (equal? "" f)) (not (eqv? #\. (string-ref f 0))))
           (process (string-append dir "/" f))))
     #f))
  (define (process file)
    (cond
     ((file-directory? file)
      (process-dir file))
     ((equal? "module" (path-extension file))
      (let* ((mod-path (path-strip-extension (path-relative file basedir)))
             (mod-name (map (lambda (x) (or (string->number x) (string->symbol x)))
                            (path-split mod-path))))
        (cond
         ((eval `(find-module ',mod-name) *config-env*)
          => (lambda (mod)
               (cond
                ((assq 'include-shared (vector-ref mod 2))
                 => (lambda (x)
                      (set! c-libs
                            (cons (cons (string-append
                                         (path-directory file)
                                         "/"
                                         (cadr x)
                                         ".c")
                                        (init-name mod-name))
                                  c-libs))))))))))))
  (process-dir basedir))

(define (include-c-lib lib)
  (display "#define sexp_init_library ")
  (display (cdr lib))
  (newline)
  (display "#include \"")
  (display (car lib))
  (display "\"")
  (newline)
  (display "#undef sexp_init_library")
  (newline)
  (newline))

(define (init-c-lib lib)
  (display "  ")
  (display (cdr lib))
  (display "(ctx sexp_api_pass(NULL, 1), env);\n"))

(define (main args)
  (find-c-libs (if (pair? (cdr args)) (cadr args) "lib"))
  (newline)
  (for-each include-c-lib c-libs)
  (newline)
  (display "static sexp sexp_init_all_libraries (sexp ctx, sexp env) {\n")
  (for-each init-c-lib c-libs)
  (display "  return SEXP_VOID;\n")
  (display "}\n\n"))

