// C23's <stdbool.h>. ccish's preprocessor pre-defines the macros
// true and false (as 1 and 0, the values C gives them), and bool and
// _Bool are C23's keywords, so this header has nothing to define; it
// exists so that #include <stdbool.h> works (doc/ccish-guide.md, the
// bool section, calls the pre-defined macros a quirk).
