#include "fastport.h"

FastPort FAST_PORTS[5]={
	(ulong*)0x3FFFC000,
	(ulong*)0x3FFFC020,
	(ulong*)0x3FFFC040,
	(ulong*)0x3FFFC060,
	(ulong*)0x3FFFC080
};

FastPort GetFastPort(const FastPortName& num){
	return FAST_PORTS[num];
}
