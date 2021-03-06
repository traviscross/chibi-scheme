;; http.scm -- http client
;; Copyright (c) 2009-2013 Alex Shinn.  All rights reserved.
;; BSD-style license: http://synthcode.com/license.txt

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; string utils

(define (string-char-index str c . o)
  (let ((start (if (pair? o) (car o) 0))
        (end (if (and (pair? o) (pair? (cdr o))) (cadr o) (string-length str))))
    (let lp ((i start))
      (cond
       ((= i end) #f)
       ((eq? c (string-ref str i)) i)
       (else (lp (+ i 1)))))))

(define (string-split str ch)
  (let ((len (string-length str)))
    (let lp ((i 0) (res '()))
      (let ((j (string-char-index str ch i)))
        (if j
            (lp (+ j 1) (cons (substring str i j) res))
            (reverse (cons (substring str i len) res)))))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; client utils

(define http-user-agent "chibi")

(define http-redirect-limit 10)
(define http-chunked-buffer-size 4096)
(define http-chunked-size-limit 409600)

(define (string-scan str ch . o)
  (let ((start (if (pair? o) (car o) 0))
        (end (if (and (pair? o) (pair? (cdr o))) (cadr o) (string-length str))))
    (let lp ((i start))
      (and (< i end)
           (if (eqv? ch (string-ref str i))
               i
               (lp (+ i 1)))))))

(define (http-parse-response line)
  (let* ((len (string-length line))
         (i (or (string-scan line #\space 0 len) len))
         (j (or (string-scan line #\space (+ i 1) len) len))
         (n (and (< i j) (string->number (substring line (+ i 1) j)))))
    (if (not (integer? n))
        (error "bad response" line i j)
        (list (substring line 0 i)
              n
              (if (>= j len) "" (substring line (+ j 1) len))))))

(define (make-generated-binary-input-port generator)
  (let ((buf #u8())
        (len 0)
        (offset 0))
    (make-custom-binary-input-port
     (lambda (bv start end)
       (let ((n (- end start)))
         (cond
          ((>= (- len offset) n)
           (bytevector-copy! bv start buf offset (+ offset n))
           (set! offset (+ offset n))
           n)
          (else
           (bytevector-copy! bv start buf offset (+ offset len))
           (let lp ((i (+ start (- len offset))))
             (set! buf (generator))
             (cond
              ((not (bytevector? buf))
               (set! buf #u8())
               (set! len 0)
               (set! offset 0)
               (- i start))
              (else
               (set! len (bytevector-length buf))
               (set! offset 0)
               (cond
                ((>= (- len offset) (- n i))
                 (bytevector-copy! bv i buf offset (+ offset (- n i)))
                 (set! offset (+ offset (- n i)))
                 n)
                (else
                 (bytevector-copy! bv i buf offset len)
                 (lp (+ i (- len offset)))))))))))))))

(define (http-wrap-chunked-input-port in)
  (define (read-chunk in)
    (let* ((line (read-line in))
           (n (and (string? line) (string->number line 16))))
      (cond
       ((not (and (integer? n) (<= 0 n http-chunked-size-limit)))
        (error "invalid chunked size line" line))
       ((zero? n) "")
       (else (read-bytevector n in)))))
  (make-generated-binary-input-port
   (lambda () (read-chunk in))))

(define (http-get/raw url in-headers limit)
  (if (<= limit 0)
      (error "http-get: redirect limit reached" url)
      (let* ((uri (if (uri? url) url (string->uri url)))
             (host (and uri (uri-host uri))))
        (if (not host)
            (error "invalid url" url)
            (let* ((io (open-net-io
                        host
                        (or (uri-port uri)
                            (if (eq? 'https (uri-scheme uri)) 443 80))))
                   (in (cadr io))
                   (out (car (cddr io))))
              (display "GET " out)
              (display (or (uri-path uri) "/") out)
              (display " HTTP/1.0\r\n" out)
              (display "Host: " out) (display host out) (display "\r\n" out)
              (cond
               ((not (assq-ref in-headers 'user-agent))
                (display "User-Agent: " out)
                (display http-user-agent out)
                (display "\r\n" out)))
              (for-each
               (lambda (x)
                 (display (car x) out)  (display ": " out)
                 (display (cdr x) out) (display "\r\n" out))
               in-headers)
              (display "Connection: close\r\n\r\n" out)
              (flush-output-port out)
              (let* ((resp (http-parse-response (read-line in)))
                     (headers (mime-headers->list in))
                     (status (quotient (cadr resp) 100)))
                (case status
                  ((2)
                   (let ((enc (assq-ref headers 'transfer-encoding)))
                     (cond
                      ((equal? enc "chunked")
                       (cons headers (http-wrap-chunked-input-port in)))
                      (else
                       (cons headers in)))))
                  ((3)
                   (close-input-port in)
                   (close-output-port out)
                   (let ((url2 (assq-ref headers 'location)))
                     (if url2
                         (http-get/raw url2 in-headers (- limit 1))
                         (error "redirect with no location header"))))
                  (else
                   (close-input-port in)
                   (close-output-port out)
                   (error "couldn't retrieve url" url resp)))))))))

(define (http-get/headers url . headers)
  (http-get/raw url
                (if (pair? headers) (car headers) '())
                http-redirect-limit))

(define (http-get url . headers)
  (cdr (apply http-get/headers url headers)))

(define (call-with-input-url url proc)
  (let* ((p (http-get url))
         (res (proc p)))
    (close-input-port p)
    res))

(define (call-with-input-url/headers url proc)
  (let* ((h+p (http-get/headers url))
         (res (proc (car h+p) (cdr h+p))))
    (close-input-port (cdr h+p))
    res))

(define (with-input-from-url url thunk)
  (let ((p (http-get url)))
    (let ((res (parameterize ((current-input-port p)) (thunk))))
      (close-input-port p)
      res)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; server utils

;; read and parse a request line
(define (http-parse-request . o)
  (let ((line (string-split
               (read-line (if (pair? o) (car o) (current-input-port)) 4096))))
    (cons (string->symbol (car line)) (cdr line))))

;; Parse a form body with a given URI and MIME headers (as parsed with
;; mime-headers->list).  Returns an alist of (name . value) for every
;; query or form parameter.
(define (http-parse-form uri headers . o)
  (let* ((in (if (pair? o) (car o) (current-input-port)))
         (type (assq-ref headers
                         'content-type
                         "application/x-www-form-urlencoded")) 
         (query0 (or (uri-query (if (string? uri) (string->uri uri) uri)) '()))
         (query (if (string? query0) (uri-query->alist query0) query0)))
    (cond
     ((and (>= (string-length type) 10)
           (string-ci=? "multipart/" (substring type 0 10)))
      (let ((mime (mime-message->sxml in headers)))
        (append
         (let lp ((ls (cddr mime))
                  (res '()))
           (cond
            ((null? ls)
             res)
            ((and (pair? (car ls))
                  (eq? 'mime (caar ls))
                  (pair? (cdar ls))
                  (pair? (car (cdar ls)))
                  (memq (caar (cdar ls)) '(^ @)))
             (let* ((disp0 (assq-ref (cdar (cdar ls)) 'content-disposition ""))
                    (disp (mime-parse-content-type disp0))
                    (name (assq-ref disp 'name)))
               (if name
                   (lp (cdr ls) (cons (cons name (cadr (cdar ls))) res))
                   (lp (cdr ls) res))))
            (else
             (lp (cdr ls) res))))
         query)))
     (else
      query))))

