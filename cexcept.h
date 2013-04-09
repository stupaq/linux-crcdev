#ifndef CEXCEPT_H
#define CEXCEPT_H

#define CATCH(exc) cexcept_catch__ ## exc ## __
#define THROW(exc) { goto CATCH(exc); }

#ifndef NDEBUG
#define TRY(code, exc) \
  if (!(code)) { fprintf(stderr, #code"; line %d;\n", __LINE__); THROW(exc); }
#else
#define TRY(code, exc) \
  if (!(code)) { THROW(exc); }
#endif

#endif //  CEXCEPT_H
