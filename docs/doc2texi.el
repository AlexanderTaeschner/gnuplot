;;;; doc2texi.el -- generate a texinfo file from the gnuplot doc file

;; Copyright (C) 1999 Bruce Ravel

;; Author:     Bruce Ravel <ravel@phys.washington.edu>
;; Maintainer: Bruce Ravel <ravel@phys.washington.edu>
;; Created:    March 23 1999
;; Updated:    May 28 1999
;; Version:    0.2
;; Keywords:   gnuplot, document, info

;; This file is not part of GNU Emacs.

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; This lisp script is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
;;
;; Permission is granted to distribute copies of this lisp script
;; provided the copyright notice and this permission are preserved in
;; all copies.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; send bug reports to the author (ravel@phys.washington.edu)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; Commentary:
;;
;; I suppose the most immediate question to ask is "Why do this in
;; emacs lisp???"  While it is true that the gnuplot.doc file lends
;; itself to processing by a 1 pass filter, there are some aspects of
;; texinfo files that are a bit tricky and would require 2 or perhaps
;; three passes.  Specifically, getting all of the cross references
;; made correctly is a lot of work.  Fortunately, texinfo-mode has
;; functions for building menus and updating nodes.  This saves a lot
;; of sweat and is the principle reason why I decided to write this is
;; emacs lisp.
;;
;; Everything else in gnuplot is written in C for the sake of
;; portability.  Emacs lisp, of course, requires that you have emacs.
;; For many gnuplot users, that is not a good assumption.  However,
;; the likelihood of needing info files in the absence of emacs is
;; very, very slim.  I think it is safe to say that someone who needs
;; info files has emacs installed and thus will be able to use this
;; program.
;;
;; Since this is emacs, I am not treating the gnuplot.doc file as a
;; text stream.  It seems much more efficient in this context to treat
;; it as a buffer.  All of the work is done by the function
;; `d2t-doc-to-texi'.  Each of the conversion chores is handled by an
;; individual function.  Each of thse functions is very similar in
;; structure.  They start at the top of the buffer, search forward for
;; a line matching the text element being converted, perform the
;; replacement in place, and move on until the end of the buffer.
;; These text manipulations are actually quite speedy.  The slow part
;; of the process is using the texinfo-mode function to update the
;; nodes and menus.  However, using these slow functions has one
;; advantage -- the texinfo-mode functions for doing menus and nodes
;; are certain to do the job correctly.  Although rather slow, this
;; approach doesn't totally suck compared to the time cost of
;; compiling and running a C program.  And the output from this is
;; much more useful than the output from the doc2info program.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; Use:
;;
;; Customize the variables at the start of the executable code.  Then
;; I intend that this be used from the command line or from a Makefile
;; like so:
;;
;;      emacs -batch -l doc2texi.el -f d2t-doc-to-texi
;;
;; or
;;
;;      emacs -batch -l doc2texi.el -f d2t-doc-to-texi-verbosely
;;
;; This will start emacs in batch mode, load this file, run the
;; converter, then quit.  This takes about 30 seconds my 133 MHz
;; Pentium.  It also sends a large number of mesages to stderr, so you
;; may want to redirect stderr to /dev/null or to a file.
;;
;; Then you can do
;;
;;      makeinfo gnuplot.info
;;
;; You may want to use the --no-split option.
;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; History:
;;
;;  0.1  Mar 23 1999 <BR> Initial version
;;  0.2  May 28 1999 <BR>
;;  0.3  Jun  2 1999 <BR> Added terminal information, fixed uref problem.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; Acknowledgements:
;;
;; Lars Hecking asked me to look into the doc -> info problem.  Silly
;; me, I said "ok."  This is what I came up with.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; To do:
;;
;;  -- internal cross-references: these are not perfect due to
;;     inconsistencies in the use of `` and case inconsistencies in
;;     the text.  The latter can be fixed.  Also I need a way to not
;;     make the @ref if point is currently in that region.
;;  -- catch errors gracefully, particularly when looking for files.
;;  -- are guesses about OS specific terminal information correct?
;;  -- turn the lists in the "What's New" and "xlabel" sections into
;;     proper lists.  "^\\([0-9]\\)+\." finds the list items.  If
;;     (match-string 1) is "1" then insert "@enumerate\n@item\n", else
;;     insert "@item\n".  also use (replace-match "").  need to find
;;     the end somehow.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; Code:

;;; You may need to customize these variables:
(defvar d2t-doc-file-name "gnuplot.doc"
  "Name of the gnuplot.doc file.")
(defvar d2t-terminal-directory (expand-file-name "../term/")
  "Location of .trm files in gnuplot source tree.")


;;; You should not need to touch anything below here ;;;;;;;;;;;;;;;;;;;;;

(require 'cl)
(eval-and-compile			; need split-string to do xrefs
  (if (fboundp 'split-string)
      ()
    (defun split-string (string &optional pattern)
      "Return a list of substrings of STRING which are separated by PATTERN.
If PATTERN is omitted, it defaults to \"[ \\f\\t\\n\\r\\v]+\"."
      (or pattern
	  (setq pattern "[ \f\t\n\r\v]+"))
      (let (parts (start 0))
	(while (string-match pattern string start)
	  (setq parts (cons (substring string start (match-beginning 0)) parts)
		start (match-end 0)))
	(nreverse (cons (substring string start) parts)))) ))

(defconst d2t-work-buffer-name "*doc2texi*"
  "Name of scratch buffer where the doc file will be converted into a
  texi file.")
(defconst d2t-scratch-buffer-name "*doc2texi-output*")
(defconst d2t-terminal-buffer-name "*doc2texi-terminal*")
(defvar d2t-verbose nil)
(defconst d2t-texi-filename "gnuplot.texi")

(defconst d2t-texi-header
  "\\input texinfo   @c -*-texinfo-*-

@c %**start of header
@setfilename gnuplot.info
@settitle Gnuplot: An Interactive Plotting Program
@setchapternewpage odd
@c %**end of header

@c define the command and options indeces
@defindex cm
@defindex op
@defindex tm

@direntry
* GNUPLOT: (gnuplot).             An Interactive Plotting Program
@end direntry

@ifnottex
@node Top, gnuplot, (dir), (dir)
@top Master Menu
@end ifnottex

@example
                       GNUPLOT

            An Interactive Plotting Program
             Thomas Williams & Colin Kelley
          Version 3.7 organized by: David Denholm

 Copyright (C) 1986 - 1993, 1998   Thomas Williams, Colin Kelley

       Mailing list for comments: info-gnuplot@@dartmouth.edu
     Mailing list for bug reports: bug-gnuplot@@dartmouth.edu

         This manual was prepared by Dick Crawford
                   3 December 1998


Major contributors (alphabetic order):
@end example

"
  "Texinfo header.")

(defconst d2t-main-menu
  "@menu
@end menu"
  "Main menu.")

(defconst d2t-texi-footer
  "@node Concept_Index, Command_Index, Bugs, Top
@unnumbered Concept Index
@printindex cp

@node Command_Index, Option_Index, Concept_Index, Top
@unnumbered Command Index
@printindex cm

@node Options_Index, Function_Index, Command_Index, Top
@unnumbered Options Index
@printindex op

@node Function_Index, Terminal_Index, Options_Index, Top
@unnumbered Function Index
@printindex fn

@node Terminal_Index, , Options_Index, Top
@unnumbered Terminal Index
@printindex tm

@c @shortcontents
@contents
@bye
"
  "Texinfo file terminator.")

(defvar d2t-level-1-alist nil
  "Alist of level 1 tags and markers.")
(defvar d2t-level-2-alist nil
  "Alist of level 2 tags and markers.")
(defvar d2t-level-3-alist nil
  "Alist of level 3 tags and markers.")
(defvar d2t-level-4-alist nil
  "Alist of level 4 tags and markers.")
(defvar d2t-level-5-alist nil
  "Alist of level 5 tags and markers.")
(defvar d2t-commands-alist nil
  "Alist of commands and markers.")
(defvar d2t-set-show-alist nil
  "Alist of options and markers.")
(defvar d2t-functions-alist nil
  "Alist of functions and markers.")
(defvar d2t-terminals-alist nil
  "Alist of terminal types and markers.")
(defvar d2t-node-list nil
  "List of nodes.")

(defvar d2t-terminal-list ())
(setq d2t-terminal-list
      '("ai"
	"cgm"
	"corel"
	"dumb"
	"dxf"
	"eepic"
	"epson"
	"fig"
	"gif"
	"hp26"
	"hp2648"
	"hp500c"
	"hpgl"
	"hpljii"
	"hppj"
	"imagen"
	"latex"
	"metafont"
	"mif"
	"pbm"
	"png"
	"post"
	"pslatex"
	"pstricks"
	"qms"
	"table"
	"tek"
	"texdraw"
	"tkcanvas"
	"tpic"))

(defun d2t-doc-to-texi-verbosely ()
  "Run `d2t-doc-to-texi' noisily"
  (interactive)
  (setq d2t-verbose t)
  (d2t-doc-to-texi))

(defun d2t-doc-to-texi ()
  "This is the doc to texi converter function.
It calls a bunch of other functions, each of which handles one
particular conversion chore."
  (interactive)
  (setq d2t-level-1-alist   nil ;; initialize variables
	d2t-level-2-alist   nil
	d2t-level-3-alist   nil
	d2t-level-4-alist   nil
	d2t-level-5-alist   nil
	d2t-commands-alist  nil
	d2t-set-show-alist  nil
	d2t-functions-alist nil
	d2t-terminals-alist nil
	d2t-node-list       nil)
  ;; open the doc file and get some data about its contents
  (d2t-prepare-workspace)
  (message "Inserting help for terminals ...")
  (d2t-get-terminals)
  (message "Analyzing doc file ...")
  (d2t-get-levels)
  (d2t-get-commands)
  (d2t-get-set-show)
  (d2t-get-functions)
  ;; convert the buffer from doc to texi, one element at a time
  (message "Converting to texinfo ...")
  (d2t-braces-atsigns)   ;; this must be the first conversion function
  (d2t-comments)         ;; delete comments
  (d2t-sectioning)       ;; chapters, sections, etc
  (d2t-indexing)         ;; index markup
  (d2t-tables)           ;; fix up tables
  (d2t-handle-html)      ;; fix up html markup
  (d2t-first-column)     ;; left justify normal text
  (d2t-enclose-examples) ;; turn indented text into @examples
  (message "Menus, nodes, xrefs ...")
  (d2t-make-refs)
  ;(d2t-make-menus)
  ;(d2t-set-nodes)
  (save-excursion        ;; fix a few more things explicitly
    (goto-char (point-min))
    (insert d2t-texi-header)
    (search-forward "@node")
    ;; (beginning-of-line)
    ;; (insert "\n\n" d2t-main-menu "\n\n")
    (search-forward "@node Old_bugs")	; `texinfo-all-menus-update' seems
    (beginning-of-line)			; to miss this one.  how odd.
    (insert "@menu\n* Old_bugs::\t\t\t\n@end menu\n\n")
    (goto-char (point-max))
    (insert d2t-texi-footer))
  (load-library "texinfo") ;; now do the hard stuff with texinfo-mode
  (texinfo-mode)
  (let ((message-log-max 0)
	(standard-output (get-buffer-create d2t-scratch-buffer-name)))
    (message "Making texinfo nodes ...\n")
    (texinfo-every-node-update)
    (message "Making texinfo menus ...\n")
    (texinfo-all-menus-update))
  (write-file d2t-texi-filename) )  ; save it and done!

(defun d2t-prepare-workspace ()
  "Create a scratch buffer and populate it with gnuplot.doc."
  (and d2t-verbose (message "  Doing d2t-prepare-workspace ..."))
  (if (get-buffer d2t-work-buffer-name)
      (kill-buffer d2t-work-buffer-name))
  (if (get-buffer d2t-texi-filename)
      (kill-buffer d2t-texi-filename))
  (if (get-buffer d2t-terminal-buffer-name)
      (kill-buffer d2t-terminal-buffer-name))
  (get-buffer-create d2t-terminal-buffer-name)
  (set-buffer (get-buffer-create d2t-work-buffer-name))
  (insert-file-contents d2t-doc-file-name)
  (goto-char (point-min)))


(defun d2t-get-terminals ()
  "Insert all appropriate terminal help."
  (let ((case-fold-search t))
    (if (string-match "linux" system-configuration)
	(setq d2t-terminal-list (append d2t-terminal-list
					'("linux"))))
    (if (string-match "amiga" system-configuration)
	(setq d2t-terminal-list (append d2t-terminal-list
					'("amiga"))))
    (if (string-match "atari" system-configuration)
	(setq d2t-terminal-list (append d2t-terminal-list
					'("atarivdi" "multitos" "atariaes"))))
    (if (string-match "mac" system-configuration)
	(setq d2t-terminal-list (append d2t-terminal-list
					'("mac"))))
    (if (string-match "beos" system-configuration)
	(setq d2t-terminal-list (append d2t-terminal-list
					'("be"))))
    (if (string-match "dos" system-configuration)
	(setq d2t-terminal-list (append d2t-terminal-list
					'("emxvga" "djsvga" "fg" "pc"))))
    (if (string-match "windows" (format "%s" system-type))
	(setq d2t-terminal-list (append d2t-terminal-list
					'("win"))))
    (if (string-match "next" system-configuration)
	(setq d2t-terminal-list (append d2t-terminal-list
					'("next"))))
    (if (string-match "os2" system-configuration)
	(setq d2t-terminal-list (append d2t-terminal-list
					'("pm" "emxvga"))))
    (if (string-match "irix" system-configuration)
	(setq d2t-terminal-list (append d2t-terminal-list
					'("iris4d"))))
    (if (string-match "sco" system-configuration)
	(setq d2t-terminal-list (append d2t-terminal-list
					'("cgi"))))
    (if (string-match "sun" system-configuration)
	(setq d2t-terminal-list (append d2t-terminal-list
					'("sun"))))
    (if (string-match "vms" system-configuration)
	(setq d2t-terminal-list (append d2t-terminal-list
					'("vws"))))
    (unless (member* system-configuration '("dos" "windows" "atari" "amiga")
		     :test 'string-match)
      (setq d2t-terminal-list
	    (append d2t-terminal-list
		    '("x11" "tgif" "gpic" "regis" "t410x" "tex" "xlib")))) )
  (setq d2t-terminal-list (sort d2t-terminal-list 'string<))
  (let ((list d2t-terminal-list) file node marker)
    (save-excursion
      (when (re-search-forward "^<4" (point-max) t)
	(beginning-of-line)
	(insert "@c ")
	(forward-line 1)
	(while list
	  (and d2t-verbose (message "    %s ..." (car list)))
	  (setq file (concat d2t-terminal-directory (car list) ".trm"))
	  (when (file-exists-p file)
	    (set-buffer d2t-terminal-buffer-name)
	    (erase-buffer)
	    (insert-file-contents file)
	    ;; find the terminal help
	    (when (search-forward "START_HELP" (point-max) "to_end")
	      (forward-line 1)
	      (delete-region (point-min) (point-marker))
	      (search-forward "END_HELP" (point-max) "to_end")
	      (beginning-of-line)
	      (delete-region (point-marker) (point-max))
	      ;; tidy up the terminal help content
	      (goto-char (point-min))
	      (while (re-search-forward "\",[ \t]*$" nil t)
		(replace-match "" nil nil))
	      (goto-char (point-min))
	      (while (re-search-forward "^\"" nil t)
		(replace-match "" nil nil))
	      (goto-char (point-min))
	      (while (re-search-forward "\\\\\"" nil t)
		(replace-match "\"" nil nil))
	      (goto-char (point-min))
	      (while (re-search-forward "^1[ \t]+\\(.+\\)$" nil t)
		(setq node   (match-string 1)
		      marker (point-marker))
		(replace-match (concat "4  " node) nil nil))
	      (goto-char (point-min))
	      (while (re-search-forward "^2" nil t)
		(replace-match "5 " nil nil))
	      (goto-char (point-min))
	      ;; set up terminals index
	      (while (re-search-forward "^\?\\([^ ]+\\)$" nil t)
		(let ((word (match-string 1)))
		  (unless (string-match "_\\|command-line-options" word)
		    (setq d2t-terminals-alist
			  (append d2t-terminals-alist
				  (list (cons word (point-marker))))))))
	      ;; and cram it into the doc buffer
	      (set-buffer d2t-work-buffer-name)
	      (insert-buffer-substring d2t-terminal-buffer-name)
	      ))
	  (setq list (cdr list))
	  )))))

;;; functions for obtaining lists of nodes in the document

(defun d2t-get-levels ()
  "Find positions of all nodes in the doc."
  (and d2t-verbose (message "  Doing d2t-get-levels ..."))
  (let ((list '("1" "2" "3" "4" "5")) str)
    (while list
      (setq str (concat "d2t-level-" (car list) "-alist"))
      (and d2t-verbose (message "    %s ..." str))
      (save-excursion
	(while (not (eobp))
	  (when (re-search-forward (concat "^" (car list) " \\(.+\\)$")
				   (point-max) "to_end")
	    (beginning-of-line)
	    (set (intern str)
		 (append (eval (intern str))
			 (list (cons (match-string 1) (point-marker)))))
	    (forward-line 1))))
      (setq list (cdr list)))))


(defun d2t-get-commands ()
  "Find all commands in the doc."
  (and d2t-verbose (message "  Doing d2t-get-commands ..."))
  (save-excursion
    (let ((alist d2t-level-1-alist) start end)
      (while alist
	(if (string= (caar alist) "Commands")
	    (setq start (cdar  alist) ;; location of "1 Commands"
		  end   (cdadr alist) ;; location of next level 1 heading
		  alist nil)
	  (setq alist (cdr alist))))
      ;;(message "%S %S" start end)
      (goto-char start)
      (while (< (point) end)
	(when (re-search-forward "^2 \\(.+\\)$" (point-max) "to_end")
	  (beginning-of-line)
	  (unless (> (point) end)
	    (setq d2t-commands-alist
		  (append d2t-commands-alist
			  (list (cons (match-string 1) (point-marker))))))
	  (forward-line 1))) )))

(defun d2t-get-set-show ()
  "Find all set-show options in the doc."
  (and d2t-verbose (message "  Doing d2t-get-set-show ..."))
  (save-excursion
    (let ((alist d2t-commands-alist) start end)
      (while alist
	(if (string= (caar alist) "set-show")
	    (setq start (cdar  alist) ;; location of "1 set-show"
		  end   (cdadr alist) ;; location of next level 2 heading
		  alist nil)
	  (setq alist (cdr alist))))
      ;;(message "%S %S" start end)
      (goto-char start)
      (while (< (point) end)
	(when (re-search-forward "^3 \\(.+\\)$" (point-max) "to_end")
	  (beginning-of-line)
	  (unless (> (point) end)
	    (setq d2t-set-show-alist
		  (append d2t-set-show-alist
			  (list (cons (match-string 1) (point-marker))))))
	  (forward-line 1))) )))


(defun d2t-get-functions ()
  "Find all functions in the doc."
  (and d2t-verbose (message "  Doing d2t-get-functions ..."))
  (let (begin end)
    (save-excursion			; determine bounds of functions
      (when (re-search-forward "^3 Functions" (point-max) "to_end")
	  (beginning-of-line)
	  (setq begin (point-marker))
	  (forward-line 1)
	  (when (re-search-forward "^3 " (point-max) "to_end")
	    (beginning-of-line)
	    (setq end (point-marker))))
      (goto-char begin)
      (while (< (point) end)
	(when (re-search-forward "^4 \\(.+\\)$" (point-max) "to_end")
	  (beginning-of-line)
	  (unless (> (point) end)
	    (setq d2t-functions-alist
		  (append d2t-functions-alist
			  (list (cons (match-string 1) (point-marker))))))
	  (forward-line 1))) )))

;; buffer manipulation functions

;; this can probably be made faster using a let-scoped alist rather
;; than the big cons block
(defun d2t-sectioning ()
  "Find all lines starting with a number.
These are chapters, sections, etc.  Delete these lines and insert the
appropriate sectioning and @node commands."
  (and d2t-verbose (message "  Doing d2t-sectioning ..."))
  (save-excursion
    (while (not (eobp))
      (re-search-forward "^\\([1-9]\\) +\\(.+\\)$" (point-max) "to_end")
      (unless (eobp)
	(let* ((number (match-string 1))
	       (word (match-string 2))
	       (node (substitute ?_ ?  word :test 'char-equal))
	       (eol  (save-excursion (end-of-line) (point-marker))))
	  ;; some node names appear twice.  make the second one unique.
	  ;; this will fail to work if a third pops up in the future!
	  (if (member* node d2t-node-list :test 'string=)
	      (setq node (concat node "_")))
	  (setq d2t-node-list (append d2t-node-list (list node)))
	  (beginning-of-line)
	  (delete-region (point-marker) eol)
	  (if (string-match "[1-4]" number) (insert "\n@node " node "\n"))
	  (cond ((string= number "1")
		 (insert "@chapter " word "\n"))
		((string= number "2")
		 (insert "@section " word "\n"))
		((string= number "3")
		 (insert "@subsection " word "\n"))
		((string= number "4")
		 (insert "@subsubsection " word "\n"))
		(t
		 (insert "\n\n@noindent --- " (upcase word) " ---\n")) ) )))))

(defun d2t-indexing ()
  "Find all lines starting with a question mark.
These are index references.  Delete these lines and insert the
appropriate indexing commands.  Only index one word ? entries,
comment out the multi-word ? entries."
  (and d2t-verbose (message "  Doing d2t-indexing ..."))
  (save-excursion
    (while (not (eobp))
      (re-search-forward "^\\\?\\([^ \n]+\\) *$" (point-max) "to_end")
      (unless (eobp)
	(let ((word (match-string 1))
	      (eol  (save-excursion (end-of-line) (point-marker))))
	  (beginning-of-line)
	  (delete-region (point-marker) eol)
	  (insert "@cindex " word "\n")
	  (cond ((assoc word d2t-commands-alist)
		 (insert "@cmindex " word "\n\n"))
		((assoc word d2t-set-show-alist)
		 (insert "@opindex " word "\n\n"))
		((assoc word d2t-terminals-alist)
		 (insert "@tmindex " word "\n\n"))
		((assoc word d2t-functions-alist)
		 (insert "@findex " word "\n\n"))) )))
    (goto-char (point-min))
    (while (not (eobp))
      (re-search-forward "^\\\?" (point-max) "to_end")
      (unless (eobp)
	(if (looking-at "functions? \\(tm_\\w+\\)")
	    (progn
	      (beginning-of-line)
	      (insert "@findex " (match-string 1) "\n@c "))
	  (beginning-of-line)
	  (insert "@c ")))) ))

(defun d2t-comments ()
  "Delete comments and lines beginning with # or %.
# and % lines are used in converting tables into various formats."
  (and d2t-verbose (message "  Doing d2t-comments ..."))
  (save-excursion
    (while (not (eobp))
      (re-search-forward "^[C#%]" (point-max) "to_end")
      (unless (eobp)
	(let ((eol  (save-excursion (end-of-line)
				    (forward-char 1)
				    (point-marker))))
	  (beginning-of-line)
	  (delete-region (point-marker) eol) )))))

(defun d2t-first-column ()
  "Justify normal text to the 0th column.
This must be run before `d2t-enclose-examples'.
This is rather slow since there are almost 9000 lines of text."
  (and d2t-verbose (message "  Doing d2t-first-column ..."))
  (save-excursion
    (while (not (eobp))
      (and (char-equal (char-after (point)) ? )
	   (delete-char 1))
      (forward-line))))

(defun d2t-braces-atsigns ()
  "Prepend @ to @, {, or } everywhere in the doc.
This MUST be the first conversion function called in
`d2t-doc-to-texi'."
  (and d2t-verbose (message "  Doing d2t-braces-atsigns ..."))
  (save-excursion
    (while (not (eobp))
      (re-search-forward "[@{}]" (point-max) "to_end")
      (unless (eobp)
	(backward-char 1)
	(insert "@")
	(forward-char 1)))))

(defun d2t-tables ()
  "Remove @start table and @end table tags.
These will be made into @example's by `d2t-enclose-examples'.
Thus, the plain text formatting already in the doc is used."
  (and d2t-verbose (message "  Doing d2t-tables ..."))
  (save-excursion
    (while (not (eobp))
      (re-search-forward "^ *@+start table" (point-max) "to_end")
      (unless (eobp)
	(let ((eol  (save-excursion (end-of-line) (point-marker))))
	  (beginning-of-line)
	  (delete-region (point-marker) eol))))
	  ;;(insert "@example")
    (goto-char (point-min))
    (while (not (eobp))
      (re-search-forward "^ *@+end table" (point-max) "to_end")
      (unless (eobp)
	(let ((eol  (save-excursion (end-of-line) (point-marker))))
	  (beginning-of-line)
	  (delete-region (point-marker) eol))))))
	  ;;(insert "@end example")


(defun d2t-enclose-examples ()
  "Turn indented text in the doc into @examples.
This must be run after `d2t-first-column'."
  (and d2t-verbose (message "  Doing d2t-enclose-examples ..."))
  (save-excursion
    (while (not (eobp))
      (re-search-forward "^ +[^ \n]" (point-max) "to_end")
      (unless (eobp)
	(beginning-of-line)
	(insert "@example\n")
	(forward-line 1)
	(re-search-forward "^[^ ]" (point-max) "to_end")
	(beginning-of-line)
	(insert "@end example\n\n") ))))

(defun d2t-handle-html ()
  "Deal with all of the html markup in the doc."
  (and d2t-verbose (message "  Doing d2t-handle-html ..."))
  (save-excursion
    (while (not (eobp))
      (let ((rx (concat "^" (regexp-quote "^")
			" *\\(<?\\)\\([^ \n]+\\)" )))
	(re-search-forward rx (point-max) "to_end")
	(unless (eobp)
	  (let ((bracket (match-string 1))
		(tag (match-string 2))
		(eol  (save-excursion (end-of-line) (point-marker))))
	    (beginning-of-line)
	    (cond
	     ;; comment out images
	     ((and (string= bracket "<") (string= tag "img"))
	      (insert "@c "))
	     ;; tyepset anchors
	     ((and (string= bracket "<") (string-match "^/?a" tag))
	      ;(insert "@c fix me!!  ")
	      (beginning-of-line)
	      (if (looking-at (concat "\\^\\s-*<a\\s-+href=" ; opening tag
				      "\"\\([^\"]+\\)\">\\s-*" ; url
				      "\\([^<]+\\)" ; text
				      "[ \t]*\\^?</a>" ; closing tag
				      ))
		  (replace-match (concat "@uref{"
					 (match-string 1)
					 ","
					 (remove* ?^ (match-string 2)
						  :test 'char-equal)
					 "}"))))
	     ;; translate <ul> </ul> to @itemize environment
	     ((and (string= bracket "<") (string-match "^ul" tag))
	      (delete-region (point) eol)
	      (insert "\n@itemize @bullet"))
	     ((and (string= bracket "<") (string-match "/ul" tag))
	      (delete-region (point) eol)
	      (insert "@end itemize\n"))
	     ;; list items
	     ((and (string= bracket "<") (string-match "^li" tag))
	      (delete-char 5)
	      (delete-horizontal-space)
	      (insert "@item\n"))
	     ;; fix up a few miscellaneous things
	     (t ;;(looking-at ".*Terminal Types")
	      (insert "@c ")) )
	    (forward-line))) ))))


(defvar d2t-dont-make-ref
  "^fit f\(x\)\\|gnuplot\\|help\\s-+plotting")
(defun d2t-make-refs ()
  "Make cross-references in the text."
  (and d2t-verbose (message "  Doing d2t-make-refs ..."))
  (let ((big-alist (append d2t-level-1-alist
			   d2t-level-2-alist
			   d2t-level-3-alist
			   d2t-level-4-alist)))
    (save-excursion
      (while (not (eobp))
	(re-search-forward "\\(`\\([^`]+\\)`\\)" (point-max) "to_end")
	(unless (eobp)
	  (let* ((b (match-beginning 1))
		 (e (match-end 1))
		 (list (split-string (match-string 2)))
		 (last (car (reverse list)))
		 (text (concat "@ref{" last "}")))
	    ;;(message "%s %s" (match-string 1) last)
	    (when (and (equal t (try-completion last big-alist))
		       (not (string= last "gnuplot")))
	      (delete-region b e)
	      (insert text))))
	))))

;;; doc2texi.el ends here
