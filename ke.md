# NAME

ke – Kyle's text editor.

## SYNPOSIS

ke \[files\]

## DESCRIPTION

ke is Kyle's text editor and is probably ill-suited to
everyone else. It was inspired by Antirez' kilo text editor by way of
someone's writeup of the process of writing a text editor from scratch.
It has keybindings inspired by VDE (and the Wordstar family) and emacs;
its spiritual parent is mg(1).

## KEYBINDINGS 

K-command mode is entered using C-k. This is taken from
Wordstar and just so happens to be blessed with starting with a most
excellent letter of grandeur. Many commands work with and without
control; for example, saving a file can be done with either C-k s or C-k
C-s. Other commands work with ESC or CTRL.

## K-COMMANDS

k-command mode can be exited with ESC or C-g.

* C-k BACKSPACE: Delete from the cursor to the beginning of the line.
* C-k SPACE: Toggle the mark.
* C-k -: If the mark is set, unindent the region.
* C-k =: If the mark is set, indent the region.
* C-k b: Switch to a buffer.
* C-k c: Close the current buffer. If no other buffers are open, an empty
  buffer will be opened. To exit, use C-k q.
* C-k d: Delete from the cursor to the end of the line.
* C-k C-d: Delete the entire line.
* C-k e: Edit a new file.
* C-k f: Flush the kill ring.
* C-k g: Go to a specific line.
* C-k j: Jump to the mark.
* C-k l: List the number of lines of code in a saved file.
* C-k m: Run make(1), reporting success or failure.
* C-k p: Switch to the next buffer.
* C-k q: Exit the editor. If the file has unsaved changes, a warning will
  be printed; a second C-k q will exit.
* C-k C-q: Immediately exit the editor.
* C-k C-r: Reload the current buffer from disk.
* C-k s: Save the file, prompting for a filename if needed.
* C-k u: Undo.
* C-k U: Redo changes (not implemented; marking this k-command as taken).
* C-k x: save the file and exit. Also C-k C-x.
* C-k y: Yank the kill ring.
* C-k \\: Dump core.

## OTHER KEYBINDINGS

* C-g: In general, C-g cancels an operation.
* C-l: Refresh the display.
* C-r: Regex search.
* C-s: Incremental find.
* C-u: Universal argument. C-u followed by numbers will repeat an operation
  n times.
* C-w: Kill the region if the mark is set.
* C-y: Yank the kill ring.
* ESC BACKSPACE: Delete the previous word.
* ESC b: Move to the previous word.
* ESC d: Delete the next word.
* ESC f: Move to the next word.
* ESC w: Save the region (if the mark is set) to the kill ring.

## FIND

The find operation is an incremental search. The up or left arrow
keys will go to the previous result, while the down or right arrow keys
will go to the next result. Unfortunately, the search starts from the
top of the file each time. This is a known bug.
