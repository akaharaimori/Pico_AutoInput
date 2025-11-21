export const COMMANDS = [
    "LABEL", "GOTO", "IF", "GOSUB", "RETURN", "WAIT", "END",
    "SET", "PRINT", "DEBUG", "REM",
    "Mode", "UseLED", "SetLED",
    "KeyPress", "KeyRelease", "KeyPushFor", "KeyType",
    "MouseMove", "MousePress", "MouseRelease", "MousePushFor", "Mouserun",
    "ProConPress", "ProConRelease", "ProConPushFor", "ProConHat", "ProConJoy"
];

export const BUILTIN_FUNCS = new Set([
    "ispressed", "rand", "gettime",
    "abs", "acos", "asin", "atan", "atan2", "ceil", "clamp", "cos", "cosh", "cot",
    "deg2rad", "e", "exp", "floor", "ln", "log10", "max", "min", "mod", "pi",
    "pow", "power", "rad2deg", "round", "sign", "sin", "sinh", "sqr", "sqrt",
    "sum", "tan", "tanh", "trunc", "fac", "fact", "ncr", "npr", "permut", "combin",
    "if", "ifs", "not", "and", "or", "true", "false", "nan", "even", "odd",
    "iseven", "isodd", "isnan", "iserr", "effect", "nominal"
]);

export const AC_CONSTANTS = {
    "MousePress": ["LEFT", "RIGHT", "MIDDLE"],
    "MouseRelease": ["LEFT", "RIGHT", "MIDDLE"],
    "MousePushFor": ["LEFT", "RIGHT", "MIDDLE"],
    "Mode": ["KeyMouse", "ProController"],
    "ProConPress": ["A", "B", "X", "Y", "L", "R", "ZL", "ZR", "MINUS", "PLUS", "HOME", "CAPTURE", "LCLICK", "RCLICK", "UP", "DOWN", "LEFT", "RIGHT"],
    "ProConRelease": ["A", "B", "X", "Y", "L", "R", "ZL", "ZR", "MINUS", "PLUS", "HOME", "CAPTURE", "LCLICK", "RCLICK"],
    "ProConPushFor": ["A", "B", "X", "Y", "L", "R", "ZL", "ZR", "MINUS", "PLUS", "HOME", "CAPTURE", "LCLICK", "RCLICK"],
    "ProConHat": ["UP", "UP_RIGHT", "RIGHT", "RIGHT_DOWN", "DOWN", "DOWN_LEFT", "LEFT", "LEFT_UP", "CENTER"],
    "KeyPress": ["F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12", "ENTER", "RETURN", "ESC", "ESCAPE", "BACKSPACE", "BKSP", "TAB", "SPACE", "SPACEBAR", "CAPSLOCK", "CAPS", "MINUS", "EQUAL", "LEFTBRACE", "RIGHTBRACE", "BACKSLASH", "SEMICOLON", "APOSTROPHE", "GRAVE", "COMMA", "DOT", "PERIOD", "SLASH", "UP", "DOWN", "LEFT", "RIGHT", "ARROWUP", "ARROWDOWN", "ARROWLEFT", "ARROWRIGHT"],
    "KeyRelease": ["F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12", "ENTER", "RETURN", "ESC", "ESCAPE", "BACKSPACE", "BKSP", "TAB", "SPACE", "SPACEBAR", "CAPSLOCK", "CAPS", "MINUS", "EQUAL", "LEFTBRACE", "RIGHTBRACE", "BACKSLASH", "SEMICOLON", "APOSTROPHE", "GRAVE", "COMMA", "DOT", "PERIOD", "SLASH", "UP", "DOWN", "LEFT", "RIGHT", "ARROWUP", "ARROWDOWN", "ARROWLEFT", "ARROWRIGHT"],
    "KeyPushFor": ["F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12", "ENTER", "RETURN", "ESC", "ESCAPE", "BACKSPACE", "BKSP", "TAB", "SPACE", "SPACEBAR", "CAPSLOCK", "CAPS", "MINUS", "EQUAL", "LEFTBRACE", "RIGHTBRACE", "BACKSLASH", "SEMICOLON", "APOSTROPHE", "GRAVE", "COMMA", "DOT", "PERIOD", "SLASH", "UP", "DOWN", "LEFT", "RIGHT", "ARROWUP", "ARROWDOWN", "ARROWLEFT", "ARROWRIGHT"]
};

// Build VALID_CONSTANTS from all values in AC_CONSTANTS, converting to uppercase
export const VALID_CONSTANTS = new Set(
    Object.values(AC_CONSTANTS).flat().map(v => v.toUpperCase())
);

// Define argument types for commands that require strict validation
// Types: "constant" = only specific constants, "string" = only string literals, "expr" = any expression
export const COMMAND_ARG_TYPES = {
    "Mode": ["constant"],           // Mode(KeyMouse) or Mode(ProController)
    "MousePress": ["constant"],     // MousePress(LEFT)
    "MouseRelease": ["constant"],   // MouseRelease(RIGHT)
    "MousePushFor": ["constant", "expr"],  // MousePushFor(LEFT, 100)
    "ProConPress": ["constant"],    // ProConPress(A)
    "ProConRelease": ["constant"],  // ProConRelease(B)
    "ProConPushFor": ["constant", "expr"],  // ProConPushFor(A, 100)
    "ProConHat": ["constant"],      // ProConHat(UP)
    "KeyPress": ["constant"],       // KeyPress(F1)
    "KeyRelease": ["constant"],     // KeyRelease(ENTER)
    "KeyPushFor": ["constant", "expr"],     // KeyPushFor(SPACE, 100)
    "KeyType": ["string", "expr", "expr"]   // KeyType("hello", 0.1, 0.1)
};
