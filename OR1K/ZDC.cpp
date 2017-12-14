//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//


#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegAllocRegistry.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Compiler.h"
#include "llvm/ADT/Statistic.h"
#include <iostream>	
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/Support/raw_ostream.h"
#include "OR1K.h"
#include "OR1KInstrInfo.h"
#include "OR1KRegisterInfo.h"
#include "OR1KMachineFunctionInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Constants.h"
#include "llvm//MC/MCInst.h"
#include <map>
#include <unordered_map>


namespace llvm{
	static cl::opt<bool> EnableZDC(
			"enable-ZDC",
			cl::init(false),
			cl::desc("Implement data-flow protection part of nZDC soft error detection scheme."),
			cl::Hidden);

	static cl::opt<bool> EnableSWIFT(
			"enable-SWIFT",
			cl::init(false),
			cl::desc("Implement data-flow protection part of SWIFT soft error detection scheme."),
			cl::Hidden);

	static cl::opt<bool> EnableSWIFTCMPprotection(
			"enable-SWIFT-WDCF",
			cl::init(false),
			cl::desc("Implement compare instruction protection of SWIFT soft error detection scheme."),
			cl::Hidden);

	static cl::opt<bool> EnableNEMESISCF(
			"enable-NEMESIS-CF",
			cl::init(false),
			cl::desc("Implement NEMESIS wrong direction control flow protection."),
			cl::Hidden);

	struct ZDC : public MachineFunctionPass {
		public:
			static char ID;
			// Pass identification, replacement for typeid
			//bool runOnMachineFunction(MachineFunction &F);
			//**********************************************************************
			// constructor
			//**********************************************************************


			ZDC() : MachineFunctionPass(ID) {    }

			//**********************************************************************
			// runOnMachineFunction
			//**********************************************************************

			std::unordered_map<unsigned int, unsigned int> registersMsrToSlvMap ={{ OR1K::R1, OR1K::R14 }, { OR1K::R2, OR1K::R15 }, {OR1K::R3, OR1K::R16 }, {OR1K::R4, OR1K::R17 }, {OR1K::R5, OR1K::R18 }, {OR1K::R6, OR1K::R19 }, {OR1K::R7, OR1K::R20}, {OR1K::R8, OR1K::R21}, {OR1K::R9, OR1K::R22},  {OR1K::R10, OR1K::R23}, {OR1K::R11, OR1K::R24}, {OR1K::R12, OR1K::R25}, {OR1K::R13, OR1K::R26}};

			std::vector<unsigned int> functionCallArgs={OR1K::R1, OR1K::R2, OR1K::R3, OR1K::R4, OR1K::R5, OR1K::R6, OR1K::R7, OR1K::R8, OR1K::R9, OR1K::R10, OR1K::R11, OR1K::R12};
			std::vector<unsigned int> cfcDirRegs={OR1K::R27, OR1K::R28}; // registers used for checking the direction of CF
			std::vector<unsigned int> cfcInstRegs={OR1K::R27, OR1K::R28}; // registers used for counting the number of master and slave instructions


			bool isGPR(unsigned int regNUM){
				std::unordered_map<unsigned int, unsigned int>::const_iterator got = registersMsrToSlvMap.find(regNUM);
				if (got == registersMsrToSlvMap.end())
				{
					return false;
				}
				return true;

			}
			unsigned int getSlaveReg(unsigned int regNUM){

				std::unordered_map<unsigned int, unsigned int>::const_iterator got = registersMsrToSlvMap.find(regNUM);
				// Check if iterator points to end of map
				if (got == registersMsrToSlvMap.end())
				{
					return regNUM;
				}
				else
				{
					return got->second; 
				}

			}

			MachineBasicBlock* makeErrorBB(MachineFunction &MF)
			{
				MachineBasicBlock *errorMBB = MF.CreateMachineBasicBlock();
				MachineFunction::iterator It = (MF.end())->getIterator();
				MF.insert(It, errorMBB);
				errorMBB->addSuccessor(errorMBB);


				//MachineInstr *MIAdd = BuildMI(MF, MF.begin()->begin()->getDebugLoc() , TII->get(OR1K::ADD)).addReg(OR1K::R22).addReg(OR1K::R22).addReg(OR1K::R22);
				//errorMBB->push_back(MIAdd);

				return errorMBB;
			}
			void checkFunctionCalls(MachineFunction &MF){

				const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
				int numBBs=0;
				for(MachineFunction::iterator MB = MF.begin(), MBE = MF.end(); (MB != MBE) ; ++MB, ++numBBs) {
					if( MF.getName() == "main" && MB == MF.begin() ){ // copies master registers to slave ones at the begining of main
						DebugLoc DL3= MB->begin()->getDebugLoc();
						// start trace
						MachineInstr *MInop100 = BuildMI(MF, DL3 , TII->get(OR1K::NOP)).addImm(100);
						MB->insert(MB->begin(), MInop100);
						for (auto reg : registersMsrToSlvMap){
							MachineInstr *copy = BuildMI(MF, DL3 , TII->get(OR1K::ADDI)).addReg(getSlaveReg(reg.first)).addReg(reg.first).addImm(0);
							MB->insert(MB->begin(), copy);
						}

					}
					for (MachineBasicBlock::iterator I=MB->begin(), E=MB->end(); I !=E ; ++I){
						DebugLoc DL3= MB->begin()->getDebugLoc();
						if (I->isCall() && std::next(I) != E){



							//1) inserts l.nop 200 before function call
							MachineInstr *MInop200 = BuildMI(MF, DL3 , TII->get(OR1K::NOP)).addImm(200);
							MB->insert(I, MInop200); 

							//1) inserts checking instructions before function call
							checkFunctionCallArguments(I, MF);

							//1) inserts l.nop 0 before function call/////
							MachineInstr *MInop = BuildMI(MF, DL3 , TII->get(OR1K::NOP)).addImm(0);
							MB->insert(I, MInop); 

							//2) inserts l.nop 100 after copying instructions
							MachineInstr *MInop100 = BuildMI(MF, DL3 , TII->get(OR1K::NOP)).addImm(100);
							MB->insertAfter(I, MInop100);
							//3) copy registers to the shadows registers after the original l.nop after function call
							for (auto reg : registersMsrToSlvMap){
								MachineInstr *copy = BuildMI(MF, DL3 , TII->get(OR1K::ADDI)).addReg(getSlaveReg(reg.first)).addReg(reg.first).addImm(0);
								MB->insertAfter(I, copy); 
							}
							//this is redundant.. However, we need this because some passes eliminate the first copy
							MachineInstr *copy = BuildMI(MF, DL3 , TII->get(OR1K::ADDI)).addReg(OR1K::R14).addReg(OR1K::R1).addImm(0);
							MB->insertAfter(I, copy);

							//this is nop after CAll
							MachineInstr *MInop0 = BuildMI(MF, DL3 , TII->get(OR1K::NOP)).addImm(0);
							MB->insertAfter(I, MInop0);



						}//end of if
					}// end of for
				}//end of for
			}// end of function
			bool isMasterReg(unsigned int reg){
				return ( getSlaveReg(reg) != reg) ? true : false;
			}
			//this function checks the function call arguments and sp, and fp (R1...R9)
			// we used register R15 and R17 to make a check sum of registers R1...R9
			// we just compare the checksums
			void checkFunctionCallArguments(MachineInstr *MI, MachineFunction &MF) {


				const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
				DebugLoc DL3= MI->getDebugLoc();
				MachineFunction::iterator MBB;

				// first two add for making check sum registers
				MachineInstr *MIcmp = NULL;
				MachineInstr *MItest = NULL;


				// comparing registers
				for (auto& Reg : functionCallArgs ){
					// jump to diagnosis BB if mismatch is observed
					MIcmp = BuildMI(MF, DL3 , TII->get(OR1K::SFNE)).addReg(Reg).addReg(getSlaveReg(Reg));
					MI->getParent()->insert(MI, MIcmp);
					MItest = BuildMI(MF, DL3 , TII->get(OR1K::BF)).addMBB(ErrorBB);
					MI->getParent()->insert(MI, MItest);
				}

			} //end-of-function


			void duplicateInstructions (MachineFunction &MF) { // duplicate computaional and load instructions with redundant registers
				for(MachineFunction::iterator MBB = MF.begin(), MBE = MF.end(); MBB != MBE; ++MBB) {
					for (MachineBasicBlock::iterator I=MBB->begin(), E=MBB->end(); I !=E ; ++I){
						if( !(I->isBranch()) &&  !(I->mayStore())  &&  !(I->isCall()) && !(I->isReturn()) && !(I->isCompare()) &&  !(I->getOpcode() == OR1K::NOP || (I->getOpcode() > 96 && I->getOpcode() < 129) /*compare*/) ){

							MachineInstr *slaveinst=  MF.CloneMachineInstr (&*I);


							slaveinst->setFlags(0);


							for (unsigned int opcount=0 ; opcount < I->getNumOperands() ;opcount++){ //
								if (I->getOperand(opcount).isReg() ){
									slaveinst->getOperand(opcount).setReg(getSlaveReg(I->getOperand(opcount).getReg())); 
								}

							} //end of for

							MBB->insert(I, slaveinst);

						}// end of if	
					}// end of for
				}//end of for
}//end of function

                                bool isOriginalInst(MachineInstr *inst){
						// this excludes error checking compare operations from original compare operations
						for (unsigned int opcount=0; opcount < inst->getNumOperands(); opcount++)// This is 2, because we just care about the first two operands of compare
							if (inst->getOperand(opcount).isReg())
								if( !isGPR(inst->getOperand(opcount).getReg())) 
									return false;
						return true;
				}
                                bool isSwiftDuplicatable(MachineInstr *inst){
					if ( inst->mayLoad() ||  inst->isBranch() || inst->mayStore() ||  inst->isCall() || inst->isReturn() || inst->isCompare() ||  (inst->getOpcode() == OR1K::NOP) ||  (inst->getOpcode() > 96 && inst->getOpcode() < 129) ) return false;
                                         inst->dump();
                                        return true;
				} //end-of-function
				void duplicateInstructionsSWIFT (MachineFunction &MF) { // duplicate computaional and load instructions with redundant registers
					const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
					for(MachineFunction::iterator MBB = MF.begin(), MBE = MF.end(); MBB != MBE; ++MBB) {
						for (MachineBasicBlock::iterator I=MBB->begin(), E=MBB->end(); I !=E ; ++I){
							if( isSwiftDuplicatable(I) ){

								MachineInstr *slaveinst=  MF.CloneMachineInstr (&*I);


								slaveinst->setFlags(0);


								for (unsigned int opcount=0 ; opcount < I->getNumOperands() ;opcount++){ //
									if (I->getOperand(opcount).isReg() ){
										slaveinst->getOperand(opcount).setReg(getSlaveReg(I->getOperand(opcount).getReg())); 
									}

								} //end of for

								MBB->insert(I, slaveinst);

							}// end of if	
							if (I->mayLoad()){
								DebugLoc DL3= I->getDebugLoc();

								MachineInstr *copyMoveM = BuildMI(MF, DL3 , TII->get(OR1K::ADDI)).addReg(getSlaveReg(I->getOperand(0).getReg())).addReg(I->getOperand(0).getReg()).addImm(0);
								MBB->insertAfter(I,copyMoveM); 
								I++; // this prevents from replication of copying move instruction

							}// end of if 
						}// end of for
					}//end of for

				} //end-of-function
                                void  insertSWIFTErrorDetectors(MachineFunction &MF) {
					const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
					for(MachineFunction::iterator MBB = MF.begin(), MBE = MF.end(); MBB != MBE; ++MBB) {
						for (MachineBasicBlock::instr_iterator I=MBB->instr_begin(), E=MBB->instr_end(); I !=E ; ++I){
                                                  DebugLoc DL3= I->getDebugLoc();
                                                    if (I->mayStore() || I->mayLoad()){
								for (unsigned int opcount=0 ; opcount < I->getNumOperands() ;opcount++)//
									if (I->getOperand(opcount).isReg()){
										int reg = I->getOperand(opcount).getReg();
                                                                                if (isGPR(reg) && !(I->mayLoad() && !opcount) ){ // the if excludes load destination registers from checking
										MachineInstr *cmpInst = BuildMI(MF, DL3 , TII->get(OR1K::SFNE)).addReg(reg).addReg(getSlaveReg(reg));
									        MBB->insert(I, cmpInst); 	
									        MachineInstr *MItest = BuildMI(MF, DL3 , TII->get(OR1K::BF)).addMBB(ErrorBB);
									        MBB->insert(I, MItest);
										}	
									}

						    } // end of if


						}// end of for
					}//end of for
				} //end-of-function
                                void  checkCMPsSWIFT(MachineFunction &MF) {
					const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
					for(MachineFunction::iterator MBB = MF.begin(), MBE = MF.end(); MBB != MBE; ++MBB) {
						for (MachineBasicBlock::instr_iterator I=MBB->instr_begin(), E=MBB->instr_end(); I !=E ; ++I){
                                                  DebugLoc DL3= I->getDebugLoc();
                                                    if (isOriginalCMP(I)){
								for (unsigned int opcount=0 ; opcount < I->getNumOperands() ;opcount++)//
									if (I->getOperand(opcount).isReg()){
										int reg = I->getOperand(opcount).getReg();
                                                                                if (isGPR(reg) && I->readsRegister(reg)){
										MachineInstr *cmpInst = BuildMI(MF, DL3 , TII->get(OR1K::SFNE)).addReg(reg).addReg(getSlaveReg(reg));
									        MBB->insert(I, cmpInst); 	
									        MachineInstr *MItest = BuildMI(MF, DL3 , TII->get(OR1K::BF)).addMBB(ErrorBB);
									        MBB->insert(I, MItest); 
										}	
									}

						    } // end of if


						}// end of for
					}//end of for
				} //end-of-function				

				void insertZDCErrorDetectors (MachineFunction &MF) { // duplicate computaional and load instructions with redundant registers
					const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
					for(MachineFunction::iterator MBB = MF.begin(), MBE = MF.end(); MBB != MBE; ++MBB) {
						for (MachineBasicBlock::instr_iterator I=MBB->instr_begin(), I1=MBB->instr_begin(), E=MBB->instr_end(); I !=E ; ++I, ++I1){
							bool check=true;
							if ( I->mayStore() ){
								DebugLoc DL3= MBB->findDebugLoc(I);
								int storeOpcode=I->getOpcode();
								int loadOpcode=0;
								int cmpOpcode=0;
								switch(storeOpcode){
									case OR1K::SW:{
											      loadOpcode=OR1K::LWZ;
											      cmpOpcode=OR1K::SFNE;
											      break;}
									case OR1K::SWA:{
											       loadOpcode=OR1K::LWA;
											       cmpOpcode=OR1K::SFNE;
											       break;}
									case OR1K::SB:{
											      loadOpcode=OR1K::LBZ;
											      cmpOpcode=OR1K::SFNE;
											      break;}
									case OR1K::SH:{
											      loadOpcode=OR1K::LHZ;
											      cmpOpcode=OR1K::SFNE;
											      break;}
									case OR1K::SWf32:{
												 loadOpcode=OR1K::LWZf32;
												 cmpOpcode=OR1K::SFNE;
												 break;}

									default:{
											//I->dump();
											errs()<< "Error Opcode not find\n";
											check=false;
										}
								}
								if(check){
									assert (loadOpcode!=0);
									assert (cmpOpcode!=0);
									MachineInstr *MIload = BuildMI(MF, DL3 , TII->get(loadOpcode));
									MIload->setFlags(0);
									for (unsigned int opcount=0; opcount < I->getNumOperands(); opcount++){ //
										MIload->addOperand(MF, I->getOperand(opcount));              
									} //end of for  

									for (unsigned int opcount=1; opcount < MIload->getNumOperands(); opcount++){ //
										if (MIload->getOperand(opcount).isReg() ){
											MIload->getOperand(opcount).setReg(getSlaveReg(I->getOperand(opcount).getReg()));
										}
									} //end of for 
									MachineInstr *MInop = BuildMI(MF, DL3 , TII->get(OR1K::NOP)).addImm(0);
									if ( std::next(I)->isBranch() || std::next(I)->isReturn() || std::next(I)->isCall() ) MBB->insertAfter(I, MInop); // nop after detector is not needed unless the next instruction is branch/jump/return/call
									MachineInstr *MItest = BuildMI(MF, DL3 , TII->get(OR1K::BF)).addMBB(ErrorBB);
									MBB->insertAfter(I, MItest); 													
									MachineInstr *MIcmp = BuildMI(MF, DL3 , TII->get(cmpOpcode), I->getOperand(0).getReg()).addReg(getSlaveReg(I->getOperand(0).getReg()));
									MBB->insertAfter(I, MIcmp); 									
									MBB->insertAfter(I, MIload);
								}

							}// end of if	
						}// end of for
					}//end of for
				} //end-of-function


				bool  isOriginalCMP(llvm::MachineBasicBlock::iterator inst){
					if ((inst->getOpcode() > 96 && inst->getOpcode() < 129) ){
						// this excludes error checking compare operations from original compare operations
						for (unsigned int opcount=0; opcount < 2; opcount++)// This is 2, because we just care about the first two operands of compare
							if (inst->getOperand(opcount).isReg())
								if( getSlaveReg(inst->getOperand(opcount).getReg()) == inst->getOperand(opcount).getReg() ) 
									return false;
						return true;
					}
					return false;

				}
				void changeCF(MachineFunction &MF) {
					const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
					for(MachineFunction::iterator MBB = MF.begin(), MBE = MF.end(); MBB != MBE ; ++MBB ) {
						DebugLoc DL3= MBB->begin()->getDebugLoc();
						llvm::MachineBasicBlock::iterator cmpInst=NULL;
						for ( llvm::MachineBasicBlock::iterator I=MBB->begin(), E=MBB->end(); I !=E ; ++I){
							if (isOriginalCMP(I)){
								cmpInst=I;
								//cmpInst->dump();
							}

							else if (I->isConditionalBranch() && cmpInst){
								assert(cmpInst);
								//cmpInst->dump();

								MachineInstr *shadowCMP=  MF.CloneMachineInstr(&*cmpInst);
								shadowCMP->setFlags(0);
								for (unsigned int opcount=0; opcount < cmpInst->getNumOperands(); opcount++)
									if (cmpInst->getOperand(opcount).isReg()) shadowCMP->getOperand(opcount).setReg(getSlaveReg(cmpInst->getOperand(opcount).getReg()));

								MachineInstr *shadowCMP1=  MF.CloneMachineInstr(&*shadowCMP);


								//create a new BB newBB: this will handle taken branches
								MachineBasicBlock *NewBB =  MF.CreateMachineBasicBlock();
								MBB->addSuccessor(NewBB);
								MachineFunction::iterator It = (MF.end())->getIterator();
								MF.insert(It,NewBB);
								// add shadow compare instruction
								NewBB->insert(NewBB->instr_begin(), shadowCMP);


								// inserts error detection instruction/branch to diagnosis routine if needed
								// the condition of inserted branch should be opposite to the Original branch
								MachineInstr *MICFDETECTION=NULL;
								if (I->getOpcode() == OR1K::BF) 
									MICFDETECTION = BuildMI(MF, DL3 , TII->get(OR1K::BNF)).addMBB(wrongDirectionErrorBB);
								else if (I->getOpcode() == OR1K::BNF) 
									MICFDETECTION = BuildMI(MF, DL3 , TII->get(OR1K::BF)).addMBB(wrongDirectionErrorBB);
								else
									I->dump();

								assert (MICFDETECTION!=NULL);

								NewBB->push_back(MICFDETECTION);
								MachineInstr *MInop = BuildMI(MF, DL3 , TII->get(OR1K::NOP)).addImm(0);
								NewBB->push_back(MInop);
								//////////////////////////////////////


								//direct jump to branch target BB in New BB, takes place if there is no CF error
								MachineInstr *MIjump = BuildMI(MF, DL3 , TII->get(OR1K::J)).addMBB(I->getOperand(0).getMBB());
								NewBB->push_back(MIjump);
								NewBB->addSuccessor(I->getOperand(0).getMBB());
								MachineInstr *MInop1 = BuildMI(MF, DL3 , TII->get(OR1K::NOP)).addImm(0);
								NewBB->push_back(MInop1);
								MachineInstr *MIjump1 = BuildMI(MF, DL3 , TII->get(OR1K::J)).addMBB(I->getOperand(0).getMBB());
								NewBB->push_back(MIjump1);

								// change the dirrection of the original conditional branch to the NewBB
								I->getOperand(0).setMBB(NewBB);     

								/////////////////////adding check if the branch is not taken
								// we insert a CMP and branch
								//The CMP operands are the shadows, and the branch is a copy of the original branch with the target destination of diagnosis block
								MachineInstr *MIcfErrorDetection = BuildMI(MF, DL3 , TII->get(I->getOpcode())).addMBB(wrongDirectionErrorBB);
								MachineInstr *MInop2 = BuildMI(MF, DL3 , TII->get(OR1K::NOP)).addImm(0);
								MBB->push_back(shadowCMP1);
								MBB->push_back(MIcfErrorDetection);
								MBB->push_back(MInop2);
								/////////////////////////////////////////
								MBB++;
								cmpInst=NULL;
								break;     
								// 
							}//end of else 

						}// end of for
					}// edn of for
				}//end of function

				MachineBasicBlock* ErrorBB=NULL;
				MachineBasicBlock* wrongDirectionErrorBB=NULL;
				bool runOnMachineFunction(MachineFunction &MF) {
					if (EnableZDC){
						ErrorBB=makeErrorBB(MF); // Data flow errors will go to this bb
						wrongDirectionErrorBB=makeErrorBB(MF); // wrong-direction control flow errors will go to this bb

						// duplicates computaional and load instructions
						duplicateInstructions(MF);

						//implement wrong-direction control-flow checking
						if (EnableNEMESISCF) changeCF(MF);

						// inserts ZDC error detectors after store instructions
						insertZDCErrorDetectors(MF);



						// this function addes the l.nop before and after function calls
						// It also insertes checking instructions for the function calls arguments
						checkFunctionCalls(MF); //BL_pred
					}
					// SWIFT DF PROTECTION TRANSFORMATION
					if (EnableSWIFT){
						ErrorBB=makeErrorBB(MF); // Data flow errors will go to this bb
						wrongDirectionErrorBB=makeErrorBB(MF); // wrong-direction control flow errors will go to this bb

						// duplicates computaional instructions and inserts copy mov instructions after load operations
						duplicateInstructionsSWIFT(MF);


						//implement SWIFT compare instruction protection
						if (EnableSWIFTCMPprotection) checkCMPsSWIFT(MF);

						// inserts SWIFT error detectors after store instructions
						insertSWIFTErrorDetectors(MF);



						// this function addes the l.nop before and after function calls
						// It also insertes checking instructions for the function calls arguments
						checkFunctionCalls(MF); //BL_pred

					}

					return true;
				}
			};

			char ZDC::ID = 0;
			FunctionPass *createZDCPass() 
			{
				return new ZDC();
			}
	}




