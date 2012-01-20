(import (scheme base) (chibi test))

;;;; http://srfi.schemers.org/srfi-62/srfi-62.html

(test-begin "srfi-62")

;; srfi-62 examples

(test '5 (+ 1 #;(* 2 3) 4))
(test '(x z) (list 'x #;'y 'z))
(test '12 (* 3 4 #;(+ 1 2)))
(test '16 (#;sqrt abs -16))
(test '(a d) (list 'a #; #;'b 'c 'd))
(test '(a e) (list 'a #;(list 'b #;c 'd) 'e))
(test '(a . c) '(a . #;b c))
(test '(a . b) '(a . b #;c))

;(test-error (#;a . b))
;(test-error (a . #;b))
;(test-error (a #;. b))
;(test-error (#;x #;y . z))
;(test-error (#; #;x #;y . z))
;(test-error (#; #;x . z))

(test-end)

