# TUI and Terminal Input

## Scope

This part documents the terminal UI/input system developed alongside the database project, especially terminal modes, escape-sequence parsing, and component navigation.

## Theory

A terminal does not necessarily deliver every key press as one semantic event. Arrow keys are commonly represented by escape sequences:

    Up    -> ESC [ A
    Down  -> ESC [ B
    Right -> ESC [ C
    Left  -> ESC [ D

In raw/non-canonical input, the program receives a stream of bytes. The input layer therefore reconstructs higher-level events from those bytes.

    Terminal -> bytes -> input state machine -> key/mouse event -> active component

## Terminal modes

A major part of the implementation was understanding termios.

- Canonical mode: terminal input is line-buffered.
- cbreak-style mode: input is delivered character-by-character while signals can remain enabled.
- Raw mode: terminal processing is minimized and the application receives input bytes directly.

For this TUI, immediate input matters because an arrow sequence must be processed before another character is typed.

The relevant termios areas are:

    c_iflag -> input transformation
    c_oflag -> output transformation
    c_lflag -> canonical mode, echo, signals, extensions
    c_cc    -> read behavior such as VMIN / VTIME

## My implementation

The input handler was designed around states such as SUPPORTS_INPUT_TEXT, SUPPORTS_NAVIGATION, and SUPPORTS_MOUSE. The handler receives individual bytes through read(char byte).

An ESC byte starts special-sequence processing. The intended flow is:

    ESC -> '[' -> navigation sequence -> A/B/C/D -> navigation handler

Mouse sequences use a different prefix and terminate after the expected mouse event bytes.

## The arrow-key bug

A key debugging step was discovering why an arrow key appeared to require pressing another key such as W/A/S/D.

The initial assumption was that the arrow key itself was producing an incomplete event. The actual issue was the interaction between terminal buffering and the parser.

In canonical mode, the program does not necessarily receive the escape sequence immediately. After switching to non-canonical/raw-style input, the remaining problem became the application's own byte-by-byte state handling.

The important observation was:

    read 1 -> ESC
    read 2 -> '['
    read 3 -> 'A'

If navigation_handle() is called before the complete sequence has been assembled, parsing sees incomplete input. The parser must therefore distinguish a sequence still being assembled from a complete navigation event.

## State-machine reasoning

The implementation accumulates special bytes using input states.

    INPUT_TEXT
        | ESC
        v
    NAVIGATION/MOUSE PREFIX
        |-- '[' -> navigation
        |-- '<' -> mouse
        v
    complete sequence
        |-- navigation_handle()
        |-- mouse_byte_handle()
        v
    INPUT_TEXT

One important design issue emerged here: SUPPORTS_NAVIGATION was being used both as a parsing state and as if it were a persistent interaction mode. Those are different concepts.

The parser state should mean: "I am currently decoding an escape sequence", not "the application is permanently in navigation mode". After a complete event is handled, the parser should return to the normal input state.

## Component interaction

The TUI has components with behavior flags such as SUPPORTS_MOUSE | SUPPORTS_NAVIGATION.

These flags are a bitmask, so checking whether a component supports a behavior should use masking:

    component.behaviour & SUPPORTS_MOUSE

rather than requiring equality with a single flag.

This became relevant when debugging why an Accordion component did not respond to navigation even though mouse interaction worked.

The intended interaction model is:

    mouse click -> active component / focus -> keyboard event -> component handles navigation

A mouse click therefore changes which component receives subsequent events; it does not itself constitute navigation.

## Design reasoning

The TUI work reinforced a broader pattern used elsewhere in the project: decode first, interpret second.

The terminal provides a byte stream. The input handler reconstructs a structured event. The component then decides what that event means.

For example:

    Terminal -> Input Handler -> KEY_UP -> Accordion -> selection--

rather than making the Accordion parse terminal escape sequences directly.

## Reading / coding path

This subsystem does not directly map to the database paper. The relevant implementation material is:

- TUI 2.txt — terminal modes, termios, cbreak/raw input, and input processing.
- TUI 3.txt — component activation, behavior flags, mouse focus, and navigation.
- Arrow Key Detection Explanation.txt — escape-sequence parsing and the byte-by-byte state machine.

**Coding path**

    termios
    -> canonical/non-canonical input
    -> VMIN / VTIME
    -> byte-by-byte stdin handler
    -> escape-sequence state machine
    -> navigation/mouse event decoding
    -> active component
    -> component-specific behavior

## Current design lesson

Terminal input is a protocol, not simply a collection of characters.

The implementation therefore needs two distinct layers:

    byte decoding -> semantic input event -> UI behavior

This is the same kind of boundary reasoning used in the database itself: lower layers decode and provide a stable abstraction, while higher layers operate on that abstraction.