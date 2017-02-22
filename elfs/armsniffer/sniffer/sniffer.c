#include "sniffer.h"
#include "system.h"

CONTEXT context;
unsigned char abt_stack[0x4000];
unsigned char sniffer_map[0x100];

unsigned int MMU_TABLE     = 0;
unsigned int  da_jumper    = 0;
unsigned int  da_address   = 0;

void (*sniff_p)(unsigned int address, unsigned int value, unsigned int pc, char is_ldr) = 0;


__arm void sniff_proc(unsigned int address, unsigned int value, unsigned int pc, char is_ldr) 
{
  if (sniff_p)
    sniff_p(address, value, pc, is_ldr);
}


__arm int emualate_ldr_str_sub(CONTEXT *context)
{ 
    unsigned int  cpsr;
    unsigned int  instruction;
    unsigned int  address = 0;
 
    //unsigned char cond;
 
    char i, l, b, h, w, s, u, p;
    char rn, rd, rm;
 
    cpsr = context->s.cpsr;
    
    // ARM
    if (!GET_PSR_T(cpsr))
    {
      // �������� ����������
      instruction = WORD(context->s.pc);
    
      // ���� �������
      //cond = IARM_COND(instruction);
   
      // ������ (LDR)
      if (IARM_IS_LDR(instruction))
      {
        p = IARM_LDR_P(instruction);
        i = IARM_LDR_I(instruction);
        u = IARM_LDR_U(instruction);
        w = IARM_LDR_W(instruction);
     
        rn = IARM_Rn(instruction);
        rd = IARM_Rd(instruction);
        rm = IARM_Rm(instruction);
     
        int sign;
        unsigned int index = 0;
       
        // ���������� ���������������� �������� ��� ���������� ������
        if (!i)
          index = IARM_Imm12(instruction);
        // ���������� �������� �� �������� ��� ���������� ������
        else 
        {
          // �������� �� ��������
          if (!IARM_ShiftImm(instruction) && !IARM_Shift(instruction))
            index = context->a[rm];
          // �������� �� �������� �� �������
          else
          {
            // �������� ��� ������
            unsigned short si = IARM_ShiftImm(instruction);
            // ��� ������
            unsigned short st = IARM_Shift(instruction);
          
            switch(st)
            {
              case IARM_Shift_LSL:
                index = context->a[rm] << si;
                break;
              case IARM_Shift_LSR:
                if (si) 
                {
                  index = 0;
                  break;
                } else
                {
                  index = context->a[rm] >> si;
                  break;
                }
            }
         }
      }
      
      // ������ ����� ���������
      if (u) sign = 1;
      // ������ ����� ��������
      else   sign = -1;

      // ���������� ����-���������� 
      if (!p)
      {
        address =  context->a[rn];
        context->a[rn] = address + index*sign;
        context->a[rd] = WORD(address - IO_ADDRESS_DIF);
        
        // ��������� ������ �� ��������
        sniff_proc(address, context->a[rd], context->s.pc, 1);
      } else
      {
        // ���������� ����-���������� 
        if (w)
        {
          address         =   context->a[rn] + index*sign;
          context->a[rn]  =   address;
          context->a[rd]  =   WORD(address - IO_ADDRESS_DIF);
          
          // ��������� ������ �� ��������
          sniff_proc(address, context->a[rd], context->s.pc, 1);
         } 
          // �� ���������� ����/����-���������� 
         else
         {  
            address         =   context->a[rn] + index*sign;
            context->a[rd]  =   WORD(address - IO_ADDRESS_DIF);
            
            // ��������� ������ �� ��������
            sniff_proc(address, context->a[rd], context->s.pc, 1);
         }
      }
    } 
    // ������ (STR)
    else if (IARM_IS_STR(instruction))
    {
      p = IARM_LDR_P(instruction);
      i = IARM_LDR_I(instruction);
      u = IARM_LDR_U(instruction);
      w = IARM_LDR_W(instruction);
     
      rn = IARM_Rn(instruction);
      rd = IARM_Rd(instruction);
      rm = IARM_Rm(instruction);
     
      int sign;
      unsigned int index = 0;
    
      // ���������� ���������������� �������� ��� ���������� ������
      if (!i) index = IARM_Imm12(instruction);
      // ���������� �������� �� �������� ��� ���������� ������
      else 
      {
        // �������� �� ��������
        if (!IARM_ShiftImm(instruction) && !IARM_Shift(instruction)) index = context->a[rm];
        // �������� �� �������� �� �������
        else
        {
            // �������� ��� ������
            unsigned short si = IARM_ShiftImm(instruction);
            // ��� ������
            unsigned short st = IARM_Shift(instruction);
          
            switch(st)
            {
              case IARM_Shift_LSL:
                index = context->a[rm] << si;
                break;
              case IARM_Shift_LSR:
                if (si) 
                {
                  index = 0;
                  break;
                } else
                {
                  index = context->a[rm] >> si;
                 break;
                }
            }
        }
      }
      
      // ������ ����� ���������
      if (u) sign = 1;
      // ������ ����� ��������
      else   sign = -1;

      // ���������� ����-���������� 
      if (!p)
      {
        address                         = context->a[rn];
        context->a[rn]                  = address + index*sign; 
        WORD(address - IO_ADDRESS_DIF)  = context->a[rd];
        
        // ��������� ������ �� ��������
        sniff_proc(address, context->a[rd], context->s.pc, 0);
      } else
      {
        // ���������� ����-���������� 
        if (w)
        {
          address                         = context->a[rn] + index*sign;
          context->a[rn]                  = address;  
          WORD(address - IO_ADDRESS_DIF)  = context->a[rd];
          
          // ��������� ������ �� ��������
          sniff_proc(address, context->a[rd], context->s.pc, 0);
        } 
        // �� ���������� ����/����-���������� 
        else
        {  
          address                         = context->a[rn] + index*sign;;
          WORD(address - IO_ADDRESS_DIF)  = context->a[rd];
          
          // ��������� ������ �� ��������
          sniff_proc(address, context->a[rd], context->s.pc, 0);
        }
      }  
    }
   
    context->s.pc += 4;
    return 1;
  } else 
  // THUMB
  {
    // �������� ����������
    instruction   = HWRD(context->s.pc);
      
    if (ITHUMB_LS_WITHREGOFFSET(instruction))
    {
        rd              =   ITHUMB_LS_WITHREGOFFSET_Rd(instruction);
        
        l               =   ITHUMB_LS_WITHREGOFFSET_L(instruction);
        b               =   ITHUMB_LS_WITHREGOFFSET_B(instruction);
            
        // ������
        if (l)
        {
            // ���������� �����
            if (b)
            context->a[rd]  =   BYTE(address - IO_ADDRESS_DIF);
            else
            // ���������� �����
            context->a[rd]  =   WORD(address - IO_ADDRESS_DIF);
            
            // ��������� ������ �� ��������
            sniff_proc(address, context->a[rd], context->s.pc, 1);
        }
        // ������   
        else
        {
            // ������ �����
            if (b)
            BYTE(address - IO_ADDRESS_DIF)   =  context->a[rd];
            else
            // ������ �����
            WORD(address - IO_ADDRESS_DIF)   =  context->a[rd];
            
            // ��������� ������ �� ��������
            sniff_proc(address, context->a[rd], context->s.pc, 0);
        }
        
    } else  if (ITHUMB_LS_SIGNEXTBYTEHWRD(instruction))
    {
        
        address         =   context->a[ITHUMB_LS_SIGNEXTBYTEHWRD_Rb(instruction)] + context->a[ITHUMB_LS_SIGNEXTBYTEHWRD_Ro(instruction)];
        rd              =   ITHUMB_LS_SIGNEXTBYTEHWRD_Rd(instruction);
        
        h               =   ITHUMB_LS_SIGNEXTBYTEHWRD_H(instruction);
        s               =   ITHUMB_LS_SIGNEXTBYTEHWRD_S(instruction);
            
        // ������ ���������
        if (!h && !s)
        {
            HWRD(address - IO_ADDRESS_DIF)   =  context->a[rd];
            
            // ��������� ������ �� ��������
            sniff_proc(address, context->a[rd], context->s.pc, 0);
        //������ ���������
        } else if (h && !s)
        {
            context->a[rd]               =   HWRD(address - IO_ADDRESS_DIF);
            
            // ��������� ������ �� ��������
            sniff_proc(address, context->a[rd], context->s.pc, 1);
        // ������ ����� � ����������� ������
        } else if (!h && s)
        {
            context->a[rd]               =   BYTE(address - IO_ADDRESS_DIF); 
            if (INS_BIT(context->a[rd], 7))
              context->a[rd] |= 0xFFFFFF00;
            
            // ��������� ������ �� ��������
            sniff_proc(address, context->a[rd], context->s.pc, 1);
        // ������ ��������� � ����������� ������
        } else if (h && s)
        {
            context->a[rd]               =   HWRD(address - IO_ADDRESS_DIF);  
            if (INS_BIT(context->a[rd], 15))
              context->a[rd] |= 0xFFFF0000;
            
            // ��������� ������ �� ��������
            sniff_proc(address, context->a[rd], context->s.pc, 1);
        }
        
    } else  if (ITHUMB_LS_WITHIMMOFFSET(instruction))
    {
        rd              =   ITHUMB_LS_WITHIMMOFFSET_Rd(instruction);
        
        l               =   ITHUMB_LS_WITHIMMOFFSET_L(instruction);
        b               =   ITHUMB_LS_WITHIMMOFFSET_B(instruction);
         
        if (b)
        address        =   context->a[ITHUMB_LS_WITHIMMOFFSET_Rb(instruction)] + ITHUMB_LS_WITHIMMOFFSET_Offset(instruction);
        else
        address        =   context->a[ITHUMB_LS_WITHIMMOFFSET_Rb(instruction)] + (ITHUMB_LS_WITHIMMOFFSET_Offset(instruction) << 2);
       
        // ������
        if (l)
        {
            // ���������� �����
            if (b)
            context->a[rd]  =   BYTE(address - IO_ADDRESS_DIF);
            else
            // ���������� �����
            context->a[rd]  =   WORD(address - IO_ADDRESS_DIF);
            
            // ��������� ������ �� ��������
            sniff_proc(address, context->a[rd], context->s.pc, 1);
        }
        // ������   
        else
        {
            // ������ �����
            if (b)
            BYTE(address - IO_ADDRESS_DIF)   =  context->a[rd];
            else
            // ������ �����
            WORD(address - IO_ADDRESS_DIF)   =  context->a[rd];
            
            // ��������� ������ �� ��������
            sniff_proc(address, context->a[rd], context->s.pc, 0);
        }
        
    } else  if (ITHUMB_LS_HWRD(instruction))
    {
        rd              =   ITHUMB_LS_HWRD_Rd(instruction);
        l               =   ITHUMB_LS_HWRD_L(instruction);
        address         =   context->a[ITHUMB_LS_HWRD_Rb(instruction)] + (ITHUMB_LS_HWRD_Offset(instruction) << 1);
        
        // ������
        if (l)
            context->a[rd]  =   HWRD(address - IO_ADDRESS_DIF);
        // ������   
        else
            HWRD(address - IO_ADDRESS_DIF)   =  context->a[rd];
        
        // ��������� ������ �� ��������
        sniff_proc(address, context->a[rd], context->s.pc, l);
    } else  if (ITHUMB_LS_SPREL(instruction))
    {
        address         =   context->s.sp + (ITHUMB_LS_SPREL_Offset(instruction) << 2);
        rd              =   ITHUMB_LS_SPREL_Rd(instruction);
        l               =   ITHUMB_LS_SPREL_L(instruction);
          
        // ������
        if (l)
          context->a[rd]  =   WORD(address - IO_ADDRESS_DIF);
        else
        // ������  
          WORD(address - IO_ADDRESS_DIF)   =  context->a[rd];
        
        // ��������� ������ �� ��������
        sniff_proc(address, context->a[rd], context->s.pc, l);
    }
     
    context->s.pc   +=  2;
    return 1;
  }  
}

__arm void emualate_ldr_str(CONTEXT *context)
{
  SetDomainAccess(0xFFFFFFFF);
  emualate_ldr_str_sub(context);
  SetDomainAccess(1);
}

void io_sniffer_init(void (*sniff_prc)(unsigned int address, unsigned int value, unsigned int pc, char is_ldr))
{
  if (sniff_prc)
  {
    UnlockAllMemoryAccess();
  
	__MRC(15, 0, MMU_TABLE, 2, 0, 0);
	
    da_jumper       =  WORD(VECTOR_DATAABORT_JUMPER_OFS);
    da_address      =  WORD(VECTOR_DATAABORT_HANDLER_OFS);
  
    WORD(VECTOR_DATAABORT_JUMPER_OFS)   =  VECTOR_DATAABORT_JUMPER;
    WORD(VECTOR_DATAABORT_HANDLER_OFS)  =  (int)&da_handler;
  
    SetMemoryAccess(1);
  
    sniff_p         =   sniff_prc;
    
    for (int i = 0; i < 0x100; i++)
      sniffer_map[i] = 0;
  }
}


void io_sniffer_deinit()
{
  UnlockAllMemoryAccess();
  
  da_jumper     =  WORD(VECTOR_DATAABORT_JUMPER_OFS);
  da_address    =  WORD(VECTOR_DATAABORT_HANDLER_OFS);
  
  WORD(VECTOR_DATAABORT_JUMPER_OFS)   =  (unsigned int)&da_jumper;
  WORD(VECTOR_DATAABORT_HANDLER_OFS)  =  (unsigned int)&da_address;
  
  for (int i = 0; i < 0x100; i++)
  {
    if (sniffer_map[i])
    {
      unsigned int io_address       = IO_ADDRESS | (i << 20) | MMU_ATTR;
      MMU_GRID(io_address)          = MMU_GRID_SETATTR(io_address);
      MMU_GRID_MIRROR(io_address)   = 0x00000000; 
    }
    
    sniffer_map[i] = 0;
  }
  
  SetMemoryAccess(1);
  
  sniff_p       =   0;
}

int io_sniffer_add(unsigned int io_address)
{
  if ((io_address >> 28) == 0x0F)
  {
    unsigned char sm    =  (io_address >> 20) & 0x0FF;
    if (!sniffer_map[sm])
    {
      UnlockAllMemoryAccess();
      
      MMU_GRID(io_address)          = 0x00000000;
      MMU_GRID_MIRROR(io_address)   = MMU_GRID_SETATTR(io_address);
      
      SetMemoryAccess(1);
      
      sniffer_map[sm] = 1;
      return 1;
    }
  }
  
  return 0;
}

int io_sniffer_remove(unsigned int io_address)
{
  if ((io_address >> 28) == 0x0F)
  {
    unsigned char sm =  (io_address >> 20) & 0x0FF;
    if (sniffer_map[sm])
    {
      UnlockAllMemoryAccess();
      
      MMU_GRID(io_address)          = MMU_GRID_SETATTR(io_address);
      MMU_GRID_MIRROR(io_address)   = 0x00000000;
      
      SetMemoryAccess(1);
      
      sniffer_map[sm] = 0;
      return 1;
    }
  }
  
  return 0;
}
