/******************************************************************
 *****                                                        *****
 *****  Name: cs8900.c                                        *****
 *****  Ver.: 1.0                                             *****
 *****  Date: 07/05/2001                                      *****
 *****  Auth: Andreas Dannenberg                              *****
 *****        HTWK Leipzig                                    *****
 *****        university of applied sciences                  *****
 *****        Germany                                         *****
 *****        adannenb@et.htwk-leipzig.de                     *****
 *****  Func: ethernet packet-driver for use with LAN-        *****
 *****        controller CS8900 from Crystal/Cirrus Logic     *****
 *****                                                        *****
 ******************************************************************/


#include "ethmac.h"
#include "LPC177x_8x.h"

static unsigned short *rxptr;
static unsigned short *txptr;

static unsigned short SwapBytes(unsigned short Data)
{
  return (Data >> 8) | (Data << 8);
} 

// CodeRed - function added to write to external ethernet PHY chip
void WriteToPHY (int reg, int writeval)
{
  unsigned int loop;
  // Set up address to access in MII Mgmt Address Register
  LPC_EMAC->MADR = DP83848C_DEF_ADR | reg;
  // Write value into MII Mgmt Write Data Register
  LPC_EMAC->MWTD = writeval;
  // Loop whilst write to PHY completes
  for (loop = 0; loop < MII_WR_TOUT; loop++) {
    if ((LPC_EMAC->MIND & MIND_BUSY) == 0) { break; }
  }
}

// CodeRed - function added to read from external ethernet PHY chip
unsigned short ReadFromPHY (unsigned char reg) 
{
  unsigned int loop;
  // Set up address to access in MII Mgmt Address Register
  LPC_EMAC->MADR = DP83848C_DEF_ADR | reg;
  // Trigger a PHY read via MII Mgmt Command Register
  LPC_EMAC->MCMD = MCMD_READ;
  // Loop whilst read from PHY completes
  for (loop = 0; loop < MII_RD_TOUT; loop++) {
    if ((LPC_EMAC->MIND & MIND_BUSY) == 0)  { break; }
  }
  LPC_EMAC->MCMD = 0; // Cancel read
  // Returned value is in MII Mgmt Read Data Register
  return (LPC_EMAC->MRDD);
}


void Init_EthMAC(void)
{
  unsigned int value, phyid1, phyid2;
  volatile unsigned int loop;
  unsigned phy_in_use = 0;
  unsigned phy_linkstatus_reg;
  unsigned phy_linkstatus_mask;  


  /* Power Up the EMAC controller. */
  LPC_SC->PCONP |= (0x1<<30);

#if RMII
  LPC_IOCON->P1_0  &= ~0x07;	/*  ENET I/O config */
  LPC_IOCON->P1_0  |= 0x01;		/* ENET_TXD0 */
  LPC_IOCON->P1_1  &= ~0x07;	
  LPC_IOCON->P1_1  |= 0x01;		/* ENET_TXD1 */
  LPC_IOCON->P1_4  &= ~0x07;
  LPC_IOCON->P1_4  |= 0x01;		/* ENET_TXEN */
  LPC_IOCON->P1_8  &= ~0x07;	
  LPC_IOCON->P1_8  |= 0x01;		/* ENET_CRS */
  LPC_IOCON->P1_9  &= ~0x07;	
  LPC_IOCON->P1_9  |= 0x01;		/* ENET_RXD0 */
  LPC_IOCON->P1_10 &= ~0x07;	
  LPC_IOCON->P1_10 |= 0x01;		/* ENET_RXD1 */
  LPC_IOCON->P1_14 &= ~0x07;	
  LPC_IOCON->P1_14 |= 0x01;		/* ENET_RX_ER */
  LPC_IOCON->P1_15 &= ~0x07;	
  LPC_IOCON->P1_15 |= 0x01;		/* ENET_REF_CLK */
#else
  LPC_IOCON->P1_0  &= ~0x07;	/*  ENET I/O config */
  LPC_IOCON->P1_0  |= 0x01;		/* ENET_TXD0 */
  LPC_IOCON->P1_1  &= ~0x07;	
  LPC_IOCON->P1_1  |= 0x01;		/* ENET_TXD1 */
  LPC_IOCON->P1_2  &= ~0x07;
  LPC_IOCON->P1_2  |= 0x01;		/* ENET_TXD2 */
  LPC_IOCON->P1_3  &= ~0x07;	
  LPC_IOCON->P1_3  |= 0x01;		/* ENET_TXD3 */
  LPC_IOCON->P1_4  &= ~0x07;
  LPC_IOCON->P1_4  |= 0x01;		/* ENET_TXEN */
  LPC_IOCON->P1_5  &= ~0x07;	
  LPC_IOCON->P1_5  |= 0x01;		/* ENET_TXER */
  LPC_IOCON->P1_6  &= ~0x07;	
  LPC_IOCON->P1_6  |= 0x01;		/* ENET_TX_CLK */
  LPC_IOCON->P1_7  &= ~0x07;	
  LPC_IOCON->P1_7  |= 0x01;		/* ENET_COL */
  LPC_IOCON->P1_8  &= ~0x07;	
  LPC_IOCON->P1_8  |= 0x01;		/* ENET_CRS */
  LPC_IOCON->P1_9  &= ~0x07;	
  LPC_IOCON->P1_9  |= 0x01;		/* ENET_RXD0 */
  LPC_IOCON->P1_10 &= ~0x07;	
  LPC_IOCON->P1_10 |= 0x01;		/* ENET_RXD1 */
  LPC_IOCON->P1_11 &= ~0x07;	
  LPC_IOCON->P1_11 |= 0x01;		/* ENET_RXD2 */
  LPC_IOCON->P1_12 &= ~0x07;	
  LPC_IOCON->P1_12 |= 0x01;		/* ENET_RXD3 */
  LPC_IOCON->P1_13 &= ~0x07;	
  LPC_IOCON->P1_13 |= 0x01;		/* ENET_RX_DV */
  LPC_IOCON->P1_14 &= ~0x07;	
  LPC_IOCON->P1_14 |= 0x01;		/* ENET_RX_ER */
  LPC_IOCON->P1_15 &= ~0x07;	
  LPC_IOCON->P1_15 |= 0x01;		/* ENET_RX_CLK/ENET_REF_CLK */
#endif

#if 1
  LPC_IOCON->P1_16 &= ~0x07;	/* ENET/PHY I/O config */
  LPC_IOCON->P1_16 |= 0x01;		/* ENET_MDC */
  LPC_IOCON->P1_17 &= ~0x07;	
  LPC_IOCON->P1_17 |= 0x01;		/* ENET_MDIO */
//  LPC_IOCON->LOC_ENET_MDIO = 0x01;
#endif
#if 0
  LPC_IOCON->P2_8 &= ~0x07;	/* ENET/PHY I/O config */
  LPC_IOCON->P2_8 |= 0x04;		/* ENET_MDC */
  LPC_IOCON->P2_9 &= ~0x07;	
  LPC_IOCON->P2_9 |= 0x04;		/* ENET_MDIO */
//  LPC_IOCON->LOC_ENET_MDIO = 0x00;
#endif

  // Set up MAC Configuration Register 1
  LPC_EMAC->MAC1 = MAC1_RES_TX | MAC1_RES_MCS_TX | MAC1_RES_RX | 
         MAC1_RES_MCS_RX |MAC1_SIM_RES | MAC1_SOFT_RES;

  // Set up MAC Command Register
  LPC_EMAC->Command = CR_REG_RES | CR_TX_RES | CR_RX_RES | CR_PASS_RUNT_FRM;
  
  // Short delay
  for (loop = 100; loop; loop--);

  // Set up MAC Configuration Register 1 to pass all receive frames
  LPC_EMAC->MAC1 = MAC1_PASS_ALL;
  // Set up MAC Configuration Register 2 to append CRC and pad out frames
  LPC_EMAC->MAC2 = MAC2_CRC_EN | MAC2_PAD_EN;

  // Set Ethernet Maximum Frame Register
  LPC_EMAC->MAXF = ETH_MAX_FLEN;
  // Set Collision Window / Retry Register
  LPC_EMAC->CLRT = CLRT_DEF;
  // Set Non Back-to-Back Inter-Packet-Gap Register
  LPC_EMAC->IPGR = IPGR_DEF;

  /* Enable Reduced MII interface. */
  LPC_EMAC->MCFG = MCFG_CLK_DIV64 | MCFG_RES_MII;
  for (loop = 100; loop; loop--);
  LPC_EMAC->MCFG = MCFG_CLK_DIV64;

  // Set MAC Command Register to enable Reduced MII interface
  // and prevent runt frames being filtered out
  LPC_EMAC->Command = CR_RMII | CR_PASS_RUNT_FRM | CR_PASS_RX_FILT;

  // Put PHY into reset mode
  WriteToPHY (PHY_REG_BMCR, 0x8000);

  // Loop until hardware reset completes
  for (loop = 0; loop < 0x100000; loop++) {
    value = ReadFromPHY (PHY_REG_BMCR);
    if (!(value & 0x8000)) {
      // Reset has completed
      break;
    }
  }

  // Just check this actually is a DP83848C PHY
  phyid1 = ReadFromPHY (PHY_REG_IDR1);
  phyid2 = ReadFromPHY (PHY_REG_IDR2);

  if (((phyid1 << 16) | (phyid2 & 0xFFF0)) == DP83848C_ID) {
    phy_in_use =  DP83848C_ID;
  }
  else if (((phyid1 << 16) | (phyid2 & 0xFFF0)) == LAN8720_ID) {
    phy_in_use = LAN8720_ID;
  }

  if (phy_in_use != 0) {	  
    // Safe to configure the PHY device

    // Set PHY to autonegotiation link speed
    WriteToPHY (PHY_REG_BMCR, PHY_AUTO_NEG);
    // loop until autonegotiation completes
    for (loop = 0; loop < 0x100000; loop++) {
      value = ReadFromPHY (PHY_REG_BMSR);
      if (value & 0x0020) {
        // Autonegotiation has completed
        break;
      }
    }
  }

  // Now check the link status
  for (loop = 0; loop < 0x10000; loop++) {
  	value = ReadFromPHY (PHY_REG_BMSR);
      if (value & 0x04) {						/* bit 2 of BSR = 1? Link Up  */
      // The link is on
      break;
    }
  }


  // Configure the EMAC with the established parameters
  switch (phy_in_use) {
  	  case DP83848C_ID:

  		  value = ReadFromPHY (PHY_REG_STS);	/* PHY Extended Status Register  */
  		  // Now configure for full/half duplex mode
  		  if (value & 0x0004) {
  		    // We are in full duplex is enabled mode
  			  LPC_EMAC->MAC2    |= MAC2_FULL_DUP;
  			  LPC_EMAC->Command |= CR_FULL_DUP;
  			  LPC_EMAC->IPGT     = IPGT_FULL_DUP;
  		  }
  		  else {
  		    // Otherwise we are in half duplex mode
  			  LPC_EMAC->IPGT = IPGT_HALF_DUP;
  		  }

  		  // Now configure 100MBit or 10MBit mode
  		  if (value & 0x0002) {
  		    // 10MBit mode
  			  LPC_EMAC->SUPP = 0;
  		  }
  		  else {
  		    // 100MBit mode
  			  LPC_EMAC->SUPP = SUPP_SPEED;
  		  }
  		  break;

  	  case LAN8720_ID:

  		  value = ReadFromPHY (PHY_REG_SCSR);	/* PHY Extended Status Register  */
  		  // Now configure for full/half duplex mode
  		  if (value & (1<<4)) {		/* bit 4: 1 = Full Duplex, 0 = Half Duplex  */
    		  // We are in full duplex is enabled mode
  			  LPC_EMAC->MAC2    |= MAC2_FULL_DUP;
  			  LPC_EMAC->Command |= CR_FULL_DUP;
  			  LPC_EMAC->IPGT     = IPGT_FULL_DUP;
  		  }else {
   		      // Otherwise we are in half duplex mode
    		  LPC_EMAC->IPGT = IPGT_HALF_DUP;
  		  }

  		  // Now configure 100MBit or 10MBit mode
  		  if (value & (1<<3)) {	/* bit 3: 1 = 100Mbps, 0 = 10Mbps  */
  			  // 100MBit mode
    		  LPC_EMAC->SUPP = SUPP_SPEED;
  		  }else {
    		  // 10MBit mode
    		  LPC_EMAC->SUPP = 0;
  		  }


  		  break;

  }

  // Now set the Ethernet MAC Address registers
  // NOTE - MAC address must be unique on the network!
  LPC_EMAC->SA0 = (MYMAC_1 << 8) | MYMAC_2; // Station address 0 Reg
  LPC_EMAC->SA1 = (MYMAC_3 << 8) | MYMAC_4; // Station address 1 Reg
  LPC_EMAC->SA2 = (MYMAC_5 << 8) | MYMAC_6; // Station address 2 Reg

  // Now initialise the Rx descriptors    
  for (loop = 0; loop < NUM_RX_FRAG; loop++) {
    RX_DESC_PACKET(loop)  = RX_BUF(loop);
    RX_DESC_CTRL(loop)    = RCTRL_INT | (ETH_FRAG_SIZE-1);
    RX_STAT_INFO(loop)    = 0;
    RX_STAT_HASHCRC(loop) = 0;
  }

  // Set up the Receive Descriptor Base address register
  LPC_EMAC->RxDescriptor    = RX_DESC_BASE;
  // Set up the Receive Status Base address register
  LPC_EMAC->RxStatus        = RX_STAT_BASE;
  // Setup the Receive Number of Descriptor register
  LPC_EMAC->RxDescriptorNumber = NUM_RX_FRAG-1;
  //  Set Receive Consume Index register to 0
  LPC_EMAC->RxConsumeIndex  = 0;

  // Now initialise the Tx descriptors 
  for (loop = 0; loop < NUM_TX_FRAG; loop++) {
    TX_DESC_PACKET(loop) = TX_BUF(loop);
    TX_DESC_CTRL(loop)   = 0;
    TX_STAT_INFO(loop)   = 0;
  }

  // Set up the Transmit Descriptor Base address register
  LPC_EMAC->TxDescriptor    = TX_DESC_BASE;
  // Set up the Transmit Status Base address register
  LPC_EMAC->TxStatus        = TX_STAT_BASE;
  // Setup the Transmit Number of Descriptor register
  LPC_EMAC->TxDescriptorNumber = NUM_TX_FRAG-1;
  //  Set Transmit Consume Index register to 0
  LPC_EMAC->TxProduceIndex  = 0;
 
  // Receive Broadcast and Perfect Match Packets



  LPC_EMAC->RxFilterCtrl = RFC_BCAST_EN | RFC_PERFECT_EN;					 
 
  // Enable interrupts MAC Module Control Interrupt Enable Register
  LPC_EMAC->IntEnable = INT_RX_DONE | INT_TX_DONE;

  // Reset all ethernet interrupts in MAC module
  LPC_EMAC->IntClear  = 0xFFFF;

  // Finally enable receive and transmit mode in ethernet core
  LPC_EMAC->Command  |= (CR_RX_EN | CR_TX_EN);
  LPC_EMAC->MAC1     |= MAC1_REC_EN;
}

// writes a word in little-endian byte order to TX_BUFFER
void WriteFrame_EthMAC(unsigned short Data)
{
  *txptr++ = Data;
}


void CopyToFrame_EthMAC(void *Source, unsigned int Size)
{
  unsigned int index;
  unsigned short *pSource;

  pSource = Source;
  Size = (Size + 1) & 0xFFFE;    // round up Size to next even number
  while (Size > 0) {
    WriteFrame_EthMAC(*pSource++);
    Size -= 2;
  }

  index = LPC_EMAC->TxProduceIndex;
  if (++index == NUM_TX_FRAG)
    index = 0;
  LPC_EMAC->TxProduceIndex = index;
}


// reads a word in little-endian byte order from RX_BUFFER

unsigned short ReadFrame_EthMAC(void)
{
  return (*rxptr++);
}


unsigned short ReadFrameBE_EthMAC(void)
{
  unsigned short ReturnValue;

  ReturnValue = SwapBytes (*rxptr++);
  return (ReturnValue);
}

void CopyFromFrame_EthMAC(void *Dest, unsigned short Size)
{
  unsigned short *pDest; 

  pDest = Dest;
  while (Size > 1) {
    *pDest++ = ReadFrame_EthMAC();
    Size -= 2;
  }
  
  if (Size) {                                         // check for leftover byte...
    *(unsigned char *)pDest = (char)ReadFrame_EthMAC();// the LAN-Controller will return 0
  }                                                   // for the highbyte
}



// does a dummy read on frame-I/O-port
// NOTE: only an even number of bytes is read!

void DummyReadFrame_EthMAC(unsigned short Size)       // discards an EVEN number of bytes
{                                                // from RX-fifo
  while (Size > 1) {
// Code Red - updated for LPC1776	  
//    ReadFrame8900();
    ReadFrame_EthMAC();
    Size -= 2;
  }
}

void RequestSend(unsigned short FrameSize)
{
  unsigned int index;
  index  = LPC_EMAC->TxProduceIndex;
  txptr = (unsigned short *)TX_DESC_PACKET(index);
  TX_DESC_CTRL(index) = FrameSize | TCTRL_LAST;
}

// check if CS8900 is ready to accept the
// frame we want to send

unsigned int Rdy4Tx(void)
{
// Code Red - updated for LPC1768
//  Write8900(ADD_PORT, PP_BusST);
//  return (Read8900(DATA_PORT) & READY_FOR_TX_NOW);

  // One the LPC the ethernet controller transmits
  // much faster than the CPU can load its buffers
  // so will always be ready to accept frame	
  return (1); 
               // 
}

// CodeRed - New function

// Reads  length of received ethernet frame and checks
// if destination address is a broadcast message or not.
// Then returns the frame length
unsigned short StartReadingFrame(void)
{
  unsigned short ReceiveLength;
  unsigned int index;

  index = LPC_EMAC->RxConsumeIndex;
  ReceiveLength = (RX_STAT_INFO(index) & RINFO_SIZE) - 3;
  rxptr = (unsigned short *)RX_DESC_PACKET(index);
  return(ReceiveLength);
}

// CodeRed - new function

void StopReadingFrame(void)
{
  unsigned int index;
  index = LPC_EMAC->RxConsumeIndex;
  if (++index == NUM_RX_FRAG) index = 0;
  LPC_EMAC->RxConsumeIndex = index;
}

// CodeRed - new function to check if frame has been received
unsigned int CheckIfFrameReceived(void)
{ 
  if (LPC_EMAC->RxProduceIndex != LPC_EMAC->RxConsumeIndex) 
    return(1); // Yes, packet received
  else 
    return(0);
}
