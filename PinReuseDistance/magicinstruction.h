/*
 * magicinstruction.h
 *
 *  Created on: Sep 29, 2009
 *      Author: dschuff
 */

#ifndef MAGICINSTRUCTION_H_
#define MAGICINSTRUCTION_H_


//CSM codes for implementing simics magic instruction
#define CSM_CODE_BREAK           55555
#define CSM_CODE_FLUSH_TLBS      55556
#define CSM_CODE_START_TIMER     55557
#define CSM_CODE_STOP_TIMER      55558
#define CSM_CODE_RANGE_DEC       55559
#define CSM_CODE_SET_FINEGRAIN_RANGE 55560
#define CSM_CODE_SET_PID         55561
#define CSM_CODE_DO_FIRSTTOUCH   55562
#define CSM_CODE_START_PERIOD    55563
#define CSM_CODE_END_PERIOD      55564
#define CSM_CODE_LOCAL_START_PERIOD 55565
#define CSM_CODE_LOCAL_END_PERIOD 55566

//these are used in software-simics iface and simics-trace processor iface
#define CHUNK_NONE -1
#define CHUNK_PRIVATE 0
#define CHUNK_STACK -2
//this one is only used in the interface between Simics and trace processor
// but as a result -3 should never be used by software-simics iface
#define CHUNK_TIMEDEC -3

#define TIMEDEC_START 1
#define TIMEDEC_STOP 2

//VOID InstrumentMagicInstruction(INS ins, VOID *v);

#endif /* MAGICINSTRUCTION_H_ */
