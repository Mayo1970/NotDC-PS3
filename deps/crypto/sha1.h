// G14: chd.c holds a SHA1 state for its writer and verifier, and the reader calls neither: the type only.
#pragma once

struct sha1_ctx { unsigned int unused; };
