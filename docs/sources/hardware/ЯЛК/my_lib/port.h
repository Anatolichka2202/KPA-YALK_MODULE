#ifndef __PORT_H_
#define __PORT_H_

#include "peripheral.h"


enum PortName{
	PORT_0=0x3FFFC000,
	PORT_1=0x3FFFC020,
	PORT_2=0x3FFFC040,
	PORT_3=0x3FFFC060,
	PORT_4=0x3FFFC080
};

typedef struct PortOption{
	uint mode;
	uint mask;	
};

class Port:Peripheral{
	private:
	enum{DIR=0,MASK=4,PIN=5,SET=6,CLR=7};
	uint mask;
	//Port(){};
	public:
		virtual Port* operator=(uint);
		Port(PortName name,uint _mask=0,uint mode=OUTPUT);
		~Port();
		virtual void Init(PortOption* opt);
		void SetValue(uint val);
		uint GetValue() const;
		//Режимы
		enum{
			INPUT=0,OUTPUT=1
		};
};
#endif