/* Copyright (C) 2017 Daniel Page <csdsp@bristol.ac.uk>
 *
 * Use of this source code is restricted per the CC BY-NC-ND license, a copy of 
 * which can be found via http://creativecommons.org (and should be included as 
 * LICENSE.txt within the associated archive or repository).
 */

#include "hilevel.h"

pcb_t procTab[ MAX_PROCS ]; pcb_t* executing = NULL;
int weights[15];

void dispatch( ctx_t* ctx, pcb_t* prev, pcb_t* next ) {
  char prev_pid = '?', next_pid = '?';

  if( NULL != prev ) {
    memcpy( &prev->ctx, ctx, sizeof( ctx_t ) ); // preserve execution context of P_{prev}
    prev_pid = '0' + prev->pid;
  }
  if( NULL != next ) {
    memcpy( ctx, &next->ctx, sizeof( ctx_t ) ); // restore  execution context of P_{next}
    next_pid = '0' + next->pid;
  }

    PL011_putc( UART0, '[',      true );
    PL011_putc( UART0, prev_pid, true );
    PL011_putc( UART0, '-',      true );
    PL011_putc( UART0, '>',      true );
    PL011_putc( UART0, next_pid, true );
    PL011_putc( UART0, ']',      true );

    executing = next;                           // update   executing process to P_{next}

  return;
}

void schedule( ctx_t* ctx) {
  

  //default 'next process' is the current process executing in case of only 1 active process
  int nextProcess = (executing->pid)-1;
  int currProcess = (executing ->pid) -1;
  int highestWeight = 0;

  //this finds the process with the highest weight and assigns it to the next process variable
  for (int i = 0; i<MAX_PROCS; i++){
    if ((currProcess+1 != procTab[i].pid) && (procTab[i].status == STATUS_READY) && (weights[i]>highestWeight)){
      nextProcess = i;
      highestWeight = weights[i];
      

    }
  }

  //this updates the ages of the current active processes and updates their weights
  for( int i = 0; i < MAX_PROCS; i++ ) {
    if (i == currProcess){
      procTab[i].age = 1;
      weights[i] = procTab[i].age * procTab[i].priority;
    }
    else if(i == nextProcess){
      procTab[i].age = 0;
      weights[i] = procTab[i].age * procTab[i].priority;
    }
    else {
      procTab[i].age = procTab[i].age + 1;
      weights[i] = procTab[i].age * procTab[i].priority;
    }
  }

  //make sure next process hasn't already been terminated and then dispatch 
  if(procTab[nextProcess].status != STATUS_TERMINATED){
    dispatch( ctx, &procTab[ currProcess ], &procTab[ nextProcess ] );
    procTab[ nextProcess ].status = STATUS_EXECUTING;
  }
  //change status of process which was switched out to ready
  if (procTab[ currProcess ].status != STATUS_TERMINATED){
    procTab[ currProcess ].status = STATUS_READY;
  }            
  

  return;
}

extern void     main_P3(); 

extern void     main_P4(); 

extern void     main_P5(); 

extern void     main_console(); 


extern uint32_t tos_user;



void hilevel_handler_rst(ctx_t* ctx             ) {
  /* Configure the mechanism for interrupt handling by
   *
   * - configuring timer st. it raises a (periodic) interrupt for each 
   *   timer tick,
   * - configuring GIC st. the selected interrupts are forwarded to the 
   *   processor via the IRQ interrupt signal, then
   * - enabling IRQ interrupts.
   */

  TIMER0->Timer1Load  = 0x00100000; // select period = 2^20 ticks ~= 1 sec
  TIMER0->Timer1Ctrl  = 0x00000002; // select 32-bit   timer
  TIMER0->Timer1Ctrl |= 0x00000040; // select periodic timer
  TIMER0->Timer1Ctrl |= 0x00000020; // enable          timer interrupt
  TIMER0->Timer1Ctrl |= 0x00000080; // enable          timer

  GICC0->PMR          = 0x000000F0; // unmask all            interrupts
  GICD0->ISENABLER1  |= 0x00000010; // enable timer          interrupt
  GICC0->CTLR         = 0x00000001; // enable GIC interface
  GICD0->CTLR         = 0x00000001; // enable GIC distributor

  for( int i = 0; i < MAX_PROCS; i++ ) {
    memset( &procTab[ i ], 0, sizeof( pcb_t ) );
    procTab[ i ].pid      = i+1;
    procTab[ i ].status   = STATUS_INVALID;
    procTab[ i ].age      = 0;
    procTab[ i ].priority = 0;
    procTab[ i ].tos      = ( uint32_t )( &tos_user + (i * 0x00001000)  );
    procTab[ i ].ctx.cpsr = 0x50;
    procTab[ i ].ctx.pc   = ( uint32_t )( 0 );
    procTab[ i ].ctx.sp   = procTab[ i ].tos;
  }

  /* Automatically execute the user programs P1 and P2 by setting the fields
   * in two associated PCBs.  Note in each case that
   *    
   * - the CPSR value of 0x50 means the processor is switched into USR mode, 
   *   with IRQ interrupts enabled, and
   * - the PC and SP values match the entry point and top of stack. 
   */

  // initialise 0-th PCB = console
  procTab[ 0 ].pid      = 1;
  procTab[ 0 ].status   = STATUS_READY;
  procTab[ 0 ].age      = 0;
  procTab[ 0 ].priority = 2;
  procTab[ 0 ].ctx.cpsr = 0x50;
  procTab[ 0 ].ctx.pc   = ( uint32_t )( &main_console );
  





  

 //set intial weights for the processes depending on their priority and age(0)
  for( int i = 0; i < MAX_PROCS; i++ ) {
    if(procTab[i].status != STATUS_INVALID){
      weights[i] = procTab[i].age * procTab[i].priority;
    }
  }
  
  
  /* Once the PCBs are initialised, we arbitrarily select the 0-th PCB to be 
   * executed: there is no need to preserve the execution context, since it 
   * is invalid on reset (i.e., no process was previously executing).
   */
  
  dispatch( ctx, NULL, &procTab[ 0 ] );

  int_enable_irq();

  return;
}

void hilevel_handler_irq(ctx_t* ctx) {

  uint32_t id = GICC0->IAR;

  // Step 4: handle the interrupt, then clear (or reset) the source.

  if( id == GIC_SOURCE_TIMER0 ) {
    PL011_putc( UART0, 'L', true ); schedule( ctx); TIMER0->Timer1IntClr = 0x01;
  }

  GICC0->EOIR = id;
  return;
}

void hilevel_handler_svc( ctx_t* ctx, uint32_t id ) {

  switch( id ) {
    case 0x00 : { // 0x00 => yield()
      schedule( ctx);

      break;
    }

    case 0x01 : { // 0x01 => write( fd, x, n )
      int   fd = ( int   )( ctx->gpr[ 0 ] );  
      char*  x = ( char* )( ctx->gpr[ 1 ] );  
      int    n = ( int   )( ctx->gpr[ 2 ] ); 

      for( int i = 0; i < n; i++ ) {
        PL011_putc( UART0, *x++, true );
      }
      
      ctx->gpr[ 0 ] = n;

      break;
    }

    case 0x04 : { // exit
      //set status of the executing process to terminated and schedule it out
      executing->status = STATUS_TERMINATED;
      schedule(ctx);

      break;
    }

    case 0x03 : { // fork
      int id;
      //find unoccupied pcb
      for( int i = 0; i < MAX_PROCS; i++ ) {
        if(procTab[i].status==STATUS_INVALID || procTab[i].status==STATUS_TERMINATED){
          id = i;
          break;
        }
      }
      
      PL011_putc( UART0, 'F',      true );
      PL011_putc( UART0, 'o',      true );
      PL011_putc( UART0, 'r',      true );
      PL011_putc( UART0, 'k',      true );

      
      

      pcb_t* pro = &procTab[id];
      //copy current context into the context of the new process
      memcpy(&pro->ctx, ctx, sizeof(ctx_t));
      //copy the stack of current process into the stack of the new process
      memcpy(&tos_user +(id * 0x00001000), &tos_user +((executing->pid)-1 * 0x00001000), sizeof(0x00001000));
      
      //set various fields for new process, execute the same program as the parent process
      procTab[id].pid = id + 1;
      procTab[id].priority = 2;
      procTab[id].age = 0;
      procTab[id].status = STATUS_READY;
      procTab[id].tos = ( uint32_t )( &tos_user + (id * 0x00001000)  );
      procTab[id].ctx.sp   = procTab[id].tos;
      procTab[id].ctx.cpsr = 0x50;
      procTab[id].ctx.pc = procTab[(executing->pid)-1].ctx.pc;

      


      

      //store id in register so execute system call can use it
      ctx->gpr[1]=id;
      
      
    }

    case 0x05 : { //exe

      char* process = (char*)(ctx->gpr[0]);
      procTab[ctx->gpr[1]].ctx.pc = (uint32_t)(process);
      
    }

    case 0x06 : { //kill
      procTab[ctx->gpr[0]-1].status = STATUS_TERMINATED;
      
    }

    case 0x07 : { //nice
      procTab[ctx->gpr[0]-1].priority = ctx->gpr[1];
    }

    default   : { // 0x?? => unknown/unsupported
      break;
    }
  }
  return;
}
