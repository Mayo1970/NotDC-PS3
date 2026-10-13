// G14: chd.c holds an MD5 state for its writer and verifier, and the reader calls neither: the type only.
#pragma once

struct MD5Context { unsigned int unused; };
