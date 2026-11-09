// A #pragma plasma import in a header doesn't make the including
// file's externs imports: notimported, used and defined nowhere, is an
// undefined reference, as without the header.
#include "imp.h"
extern int notimported;
int main() { return notimported; }
