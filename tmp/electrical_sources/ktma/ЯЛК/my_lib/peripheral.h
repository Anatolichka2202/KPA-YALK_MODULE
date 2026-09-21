#ifndef __Perepherial_H_
#define __Perepherial_H_
#include "type.h"
#include <LPC23xx.h>

/*
class POption(){
	virtual bool Valid()=0;
}

  */
class Peripheral{
	protected:
		ulong *base_addr;
	public:
		
		Peripheral(ulong *addr):base_addr(addr){};
		~Peripheral(){};
		//	virtual bool Init(POption* opt=NULL)=0; 
		//virtual void Init()=0;
		//virtual uint GetIntStatus()=0 const;
};

#endif
