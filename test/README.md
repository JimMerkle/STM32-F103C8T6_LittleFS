# test

Host-side test scripts that talk to the Blue Pill over the console serial port
(Pico debugprobe UART bridge, COM7, 115200 8N1).

Close Tera Term, or any other program holding the port, before running them.
They need Python 3 and pyserial (`pip install pyserial`).

## test_history.py

Checks the command line history and line editing in `Core/Src/command_line.c`
(`COMMAND_LINE_HISTORY` enabled).

```
python test_history.py COM7
```

Each test types an edited single-word command and presses Enter. The firmware replies
`Command "<word>" not found`, so the echoed word shows exactly what the line editor
produced. No real command is run.

Covered: up/down recall, wrap-around after more than 10 entries (`HISTORY_DEPTH`), draft
line restored when stepping down past the newest entry, duplicate suppression, insert at
cursor, Delete, Backspace mid-line, Home/End (`ESC [ 1~ / 4~` and `ESC [ H / F`), and an
escape sequence whose bytes arrive 30 ms apart.

Not covered: how the line looks on a real terminal (redraw artifacts). Check that by eye in Tera Term.

The script exits with status 0 when every test passes.
