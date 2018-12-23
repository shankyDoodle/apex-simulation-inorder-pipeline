/*
 *  cpu.c
 *  Contains APEX cpu pipeline implementation
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"

/* Set this flag to 1 to enable debug messages */
int ENABLE_DEBUG_MESSAGES = 0;

/* Set this flag to 1 to enable debug messages*/
#define DATA_FORWARDING_ENABLED 1

/*
 * This function creates and initializes APEX cpu.
 *
 * Note : You are free to edit this function according to your
 * 				implementation
 */
APEX_CPU *APEX_cpu_init(const char *filename) {
  if (!filename) {
    return NULL;
  }

  APEX_CPU *cpu = malloc(sizeof(*cpu));
  if (!cpu) {
    return NULL;
  }

  /* Initialize PC, Registers and all pipeline stages */
  cpu->pc = 4000;
  memset(cpu->regs, 0, sizeof(int) * 32);
  memset(cpu->regs_valid, 1, sizeof(int) * 32);
  memset(cpu->stage, 0, sizeof(CPU_Stage) * NUM_STAGES);
  memset(cpu->data_memory, 0, sizeof(int) * 4000);

  /* Parse input file and create code memory */
  cpu->code_memory = create_code_memory(filename, &cpu->code_memory_size);

  if (!cpu->code_memory) {
    free(cpu);
    return NULL;
  }

  if (ENABLE_DEBUG_MESSAGES) {
    fprintf(stderr,
            "APEX_CPU : Initialized APEX CPU, loaded %d instructions\n",
            cpu->code_memory_size);
    fprintf(stderr, "APEX_CPU : Printing Code Memory\n");
    printf("%-9s %-9s %-9s %-9s %-9s\n", "opcode", "rd", "rs1", "rs2", "imm");

    for (int i = 0; i < cpu->code_memory_size; ++i) {
      printf("%-9s %-9d %-9d %-9d %-9d\n",
             cpu->code_memory[i].opcode,
             cpu->code_memory[i].rd,
             cpu->code_memory[i].rs1,
             cpu->code_memory[i].rs2,
             cpu->code_memory[i].imm);
    }
  }

  /* Make all stages busy except Fetch stage, initally to start the pipeline */
  for (int i = 1; i < NUM_STAGES; ++i) {
    cpu->stage[i].busy = 1;
  }

  return cpu;
}

/*
 * This function de-allocates APEX cpu.
 *
 * Note : You are free to edit this function according to your
 * 				implementation
 */
void APEX_cpu_stop(APEX_CPU *cpu) {
  free(cpu->code_memory);
  free(cpu);
}

/* Converts the PC(4000 series) into
 * array index for code memory
 *
 * Note : You are not supposed to edit this function
 *
 */
int get_code_index(int pc) {
  return (pc - 4000) / 4;
}

/**
 *
 * @param stage
 */
static void print_instruction(CPU_Stage *stage) {
  if (strcmp(stage->opcode, "STORE") == 0) {
    printf("%s,R%d,R%d,#%d ", stage->opcode, stage->rs1, stage->rs2, stage->imm);
  }

  if (strcmp(stage->opcode, "LOAD") == 0) {
    printf("%s,R%d,R%d,#%d ", stage->opcode, stage->rd, stage->rs1, stage->imm);
  }

  if (strcmp(stage->opcode, "MOVC") == 0) {
    printf("%s,R%d,#%d ", stage->opcode, stage->rd, stage->imm);
  }

  if (strcmp(stage->opcode, "JUMP") == 0) {
    printf("%s,R%d,#%d ", stage->opcode, stage->rs1, stage->imm);
  }

  if (strcmp(stage->opcode, "HALT") == 0) {
    printf("%s", stage->opcode);
  }

  if (
      strcmp(stage->opcode, "BZ") == 0 ||
      strcmp(stage->opcode, "BNZ") == 0
      ) {
    printf("%s,#%d ", stage->opcode, stage->imm);
  }

  if (strcmp(stage->opcode, "NOP") == 0) {
    printf("%s ", stage->opcode);
  }

  if (
      strcmp(stage->opcode, "ADD") == 0 ||
      strcmp(stage->opcode, "SUB") == 0 ||
      strcmp(stage->opcode, "MUL") == 0 ||
      strcmp(stage->opcode, "AND") == 0 ||
      strcmp(stage->opcode, "EX-OR") == 0 ||
      strcmp(stage->opcode, "OR") == 0
      ) {
    printf("%s,R%d,R%d,R%d ", stage->opcode, stage->rd, stage->rs1, stage->rs2);
  }

}

/* Debug function which dumps the cpu stage
 * content
 *
 * Note : You are not supposed to edit this function
 *
 */
static void print_stage_content(char *name, CPU_Stage *stage) {
  printf("%-15s: pc(%d) ", name, stage->pc);
  print_instruction(stage);
  printf("\n");
}

/**
 * Flush stage and replace with empty
 * @param cpu
 * @param stageName
 */
void flushStageWithEmpty(APEX_CPU *cpu, enum myEnum stageName) {
  CPU_Stage *stage = &cpu->stage[stageName];

  /*free reg_valid*/
  cpu->regs_valid[stage->rd] = 1;

  /*reset all stage related data*/
  strcpy(stage->opcode, "");
  stage->rd = 999;
  stage->rs1 = 999;
  stage->rs2 = 999;
  stage->imm = 999;
  stage->rdPrev = 999;
}

/**
 * Flush stage and replace with NOP
 * @param cpu
 * @param stageName
 * @param dontChangeValid
 */
void flushStageWithNOP(APEX_CPU *cpu, enum myEnum stageName, int dontChangeValid, int fromJump) {
  CPU_Stage *stage = &cpu->stage[stageName];

  /*free reg_valid*/
  if (!dontChangeValid) {
    cpu->regs_valid[stage->rd] = 1;
  }

  /*reset all stage related data*/
  strcpy(stage->opcode, "NOP");
  stage->rd = 999;
  stage->rs1 = 999;
  stage->rs2 = 999;
  stage->imm = 999;
  stage->rdPrev = 999;

  if(fromJump){
    stage->stallDueToNextStage = 0;
    stage->flushInNextStage = 0;
    stage->handleJumpInNextStage = 0;
    stage->handleBZInNextStage = 0;
    stage->handleBNZInNextStage = 0;
    stage->stallDueToLoadFlag = 0;
  }
}

/**
 * BZ n BNZ pc counter update handling
 * @param cpu
 * @param stageName
 */
void bzBnzBranchHandling(APEX_CPU *cpu, enum myEnum stageName) {
  CPU_Stage *stage = &cpu->stage[stageName];

  //TODO: Check this line.: 8 is reduced to handle the PC increased by F n DRF stage instructions
  cpu->pc += (stage->imm - 8) - 4;
  cpu->ins_completed += stage->imm / 4 - 1;
  stage->flushInNextStage = 1;
}

/**
 * Fetch Stage of APEX Pipeline
 * @param cpu
 * @return
 */
int fetch(APEX_CPU *cpu) {
  CPU_Stage *stage = &cpu->stage[F];

  if (!cpu->stage[DRF].stalled && stage->stalled && stage->stallDueToNextStage) {
    stage->stallDueToNextStage = 0;
    stage->stalled = 0;
    cpu->stage[DRF] = cpu->stage[F];
    if (ENABLE_DEBUG_MESSAGES) {
      print_stage_content("Fetch", stage);
    }
    return 0;
  }

  if (!stage->busy && !stage->stalled) {
    /* Store current PC in fetch latch */
    stage->pc = cpu->pc;

    /* Index into code memory using this pc and copy all instruction fields into fetch latch*/
    APEX_Instruction *current_ins = &cpu->code_memory[get_code_index(cpu->pc)];

    /** Accept only valid instructions*/
    if(
        !(strcmp(current_ins->opcode, "") == 0 ||
          strcmp(current_ins->opcode, "ADD") == 0 ||
          strcmp(current_ins->opcode, "SUB") == 0 ||
          strcmp(current_ins->opcode, "MUL") == 0 ||
          strcmp(current_ins->opcode, "AND") == 0 ||
          strcmp(current_ins->opcode, "OR") == 0 ||
          strcmp(current_ins->opcode, "EX-OR") == 0 ||
          strcmp(current_ins->opcode, "MOVC") == 0 ||
          strcmp(current_ins->opcode, "STORE") == 0 ||
          strcmp(current_ins->opcode, "LOAD") == 0 ||
          strcmp(current_ins->opcode, "BZ") == 0 ||
          strcmp(current_ins->opcode, "BNZ") == 0 ||
          strcmp(current_ins->opcode, "JUMP") == 0 ||
          strcmp(current_ins->opcode, "HALT") == 0)
        ){
      return 0;
    }


    strcpy(stage->opcode, current_ins->opcode);
    stage->rd = current_ins->rd;
    stage->rs1 = current_ins->rs1;
    stage->rs2 = current_ins->rs2;
    stage->imm = current_ins->imm;
    stage->rdPrev = 999;// dummy garbage value

    /* Update PC for next instruction */
    cpu->pc += 4;

    if (cpu->haltFlag) {
      flushStageWithEmpty(cpu, F);
    }

    /* Copy data from fetch latch to decode latch*/
    if (!cpu->stage[DRF].stalled && !stage->stalled) {
      cpu->stage[DRF] = cpu->stage[F];
    }

    if (!stage->stalled && cpu->stage[DRF].stalled) {
      stage->stalled = 1;
      stage->stallDueToNextStage = 1;
    }

    /*if (ENABLE_DEBUG_MESSAGES) {
      print_stage_content("Fetch", stage);
    }*/
  }

  if (ENABLE_DEBUG_MESSAGES) {
    print_stage_content("Fetch", stage);
  }
  return 0;
}

/**
 * Decode Stage of APEX Pipeline
 * @param cpu
 * @return
 */
int decode(APEX_CPU *cpu) {
  CPU_Stage *stage = &cpu->stage[DRF];

  /** If source regs are valid and available then remove stall of this stage */
  if(strcmp(stage->opcode, "JUMP") != 0 && strcmp(stage->opcode, "LOAD") != 0){
    if ((stage->rs1 >= 0 && stage->rs1 < 1000 && cpu->regs_valid[stage->rs1] != 999) &&
        (stage->rs2 >= 0 && stage->rs2 < 1000 && cpu->regs_valid[stage->rs2] != 999)) {
      stage->stalled = 0;
    }
  }

  /** JUMP & LOAD instruction has only one src reg, so checked separately*/
  if ((strcmp(stage->opcode, "JUMP") == 0 || strcmp(stage->opcode, "LOAD") == 0) &&
      stage->rs1 >= 0 && stage->rs1 < 1000 && cpu->regs_valid[stage->rs1] != 999) {
    stage->stalled = 0;
  }

  /**Check stall status due to Next instruction*/
  if (!cpu->stage[EX].stalled && stage->stalled && stage->stallDueToNextStage) {
    stage->stallDueToNextStage = 0;
    stage->stalled = 0;
  }

  /**Check stall status due to BZ instruction*/
  int justRemovedBZStall = 0;
  if (stage->stalled && strcmp(stage->opcode, "BZ") == 0 && cpu->regs_valid[stage->rdPrev] == 1) {
    stage->stalled = 0;
    stage->rdPrev = 999; //reset rdPrev data;
    justRemovedBZStall = 1;
  }

  /**Check stall status due to BNZ instruction*/
  int justRemovedBNZStall = 0;
  if (stage->stalled && strcmp(stage->opcode, "BNZ") == 0 && cpu->regs_valid[stage->rdPrev] == 1) {
    stage->stalled = 0;
    stage->rdPrev = 999; //reset rdPrev data;
    justRemovedBNZStall = 1;
  }

  /** Check stall due to load store forwarding scenario*/
  if(stage->stalled && stage->stallDueToLoadFlag){
    stage->stalled = 0;
  }

  int getDataForwardedSrcValues = 0;

  if (!stage->busy && !stage->stalled) {

    /** check for normal flow dependency */
    if (strcmp(stage->opcode, "") != 0 &&
        (cpu->regs_valid[stage->rs1] == 999 || cpu->regs_valid[stage->rs2] == 999)) {
      if (!DATA_FORWARDING_ENABLED) {
        /**If data forwarding is not enabled then stall the stage and go for orthodox way*/
        stage->stalled = 1;
      } else {
        /** If data forwarding is enabled then check for next stages and their buffer values*/
        getDataForwardedSrcValues = 1;
      }
    }

    /** set dependency flag */
    if (strcmp(stage->opcode, "") != 0 &&
        !stage->stalled && stage->rd >= 0 && stage->rd < 1000) {
      cpu->regs_valid[stage->rd] = 999;
    }

    /* No Register file read needed for MOVC */
    if (strcmp(stage->opcode, "MOVC") == 0) {
      /*No Operation here*/
    }

    /* No Register file read needed for JUMP */
    if (strcmp(stage->opcode, "JUMP") == 0) {
      stage->rs1_value = cpu->regs[stage->rs1];
    }

    /* No Register file read needed for HALT */
    if (strcmp(stage->opcode, "HALT") == 0) {
      //no action here. handling done in next stage.
    }

    /* Read data from register file for store */
    if (strcmp(stage->opcode, "STORE") == 0) {
      stage->rs1_value = cpu->regs[stage->rs1];
      stage->rs2_value = cpu->regs[stage->rs2];
    }

    /* Read data from register file for store */
    if (strcmp(stage->opcode, "LOAD") == 0) {
      stage->rs1_value = cpu->regs[stage->rs1];
    }

    /* Read data from register file for
     * ADD, SUB, MUL, AND, OR ,EX-OR*/
    if (
        strcmp(stage->opcode, "ADD") == 0 ||
        strcmp(stage->opcode, "SUB") == 0 ||
        strcmp(stage->opcode, "MUL") == 0 ||
        strcmp(stage->opcode, "AND") == 0 ||
        strcmp(stage->opcode, "EX-OR") == 0 ||
        strcmp(stage->opcode, "OR") == 0
        ) {
      stage->rs1_value = cpu->regs[stage->rs1];
      stage->rs2_value = cpu->regs[stage->rs2];
    }

    /* BZ */
    if (strcmp(stage->opcode, "BZ") == 0 && !DATA_FORWARDING_ENABLED) {
      if (justRemovedBZStall == 1) {
        stage->handleBZInNextStage = 1;
        stage->zFlag = cpu->zFlag;
      } else {
        stage->stalled = 1;
        stage->rdPrev = cpu->stage[EX].rd;
        stage->handleBZInNextStage = 0;
        stage->zFlag = 999;
      }
    }

    /* BNZ */
    if (strcmp(stage->opcode, "BNZ") == 0 && !DATA_FORWARDING_ENABLED) {
      if (justRemovedBNZStall == 1) {
        stage->handleBNZInNextStage = 1;
        stage->zFlag = cpu->zFlag;
      } else {
        stage->stalled = 1;
        stage->rdPrev = cpu->stage[EX].rd;
        stage->handleBNZInNextStage = 0;
        stage->zFlag = 999;
      }
    }

    if (getDataForwardedSrcValues) {
      int valueFilled = 0;
      int justStalledForBonusLoad = 0;

      /**Main Logic*/
      if (cpu->regs_valid[stage->rs1] == 999) {

        if(strcmp(stage->opcode, "STORE") == 0 && stage->rs1 != stage->rs2){
          if (cpu->stage[MEM].rd == stage->rs1) {
            stage->rs1_value = cpu->stage[MEM].buffer;
          }

          //If dependency is due to instruction from stage EX then take its buffer value
          else if (cpu->stage[WB].rd == stage->rs1) {
            stage->rs1_value = cpu->stage[WB].buffer;
          }
          valueFilled = 1;
          goto endOfIf;
        }

        /**If dependency is due to instruction from stage EX then take its buffer value*/
        if (cpu->stage[EX].rd == stage->rs1) {
          if(stage->stallDueToLoadFlag){
            if(strcmp(cpu->stage[MEM].opcode, "LOAD") == 0){
              stage->rs1_value = cpu->data_memory[cpu->stage[MEM].mem_address];
              stage->stalled = 0;
              stage->stallDueToLoadFlag = 0;
              valueFilled = 1;
            } else{
              stage->stalled=1;
            }

          }else{
            stage->rs1_value = cpu->stage[EX].buffer;
            if(strcmp(cpu->stage[EX].opcode, "LOAD") == 0){
              stage->stalled = 1;
              stage->stallDueToLoadFlag = 1;
              justStalledForBonusLoad = 1;
              valueFilled = 0;
            }else{
              valueFilled = 1;
            }
          }
        }

          /**If dependency is due to instruction from stage EX then take its buffer value*/
        else if (cpu->stage[MEM].rd == stage->rs1) {
          stage->rs1_value = cpu->stage[MEM].buffer;
          valueFilled = 1;
        }

          /**If dependency is due to instruction from stage EX then take its buffer value*/
        else if (cpu->stage[WB].rd == stage->rs1) {
          stage->rs1_value = cpu->stage[WB].buffer;
          valueFilled = 1;
        }

        endOfIf:;
      }

      if (cpu->regs_valid[stage->rs2] == 999 && !justStalledForBonusLoad) {

        /**If dependency is due to instruction from stage EX then take its buffer value*/
        if (cpu->stage[EX].rd == stage->rs2) {
          if(stage->stallDueToLoadFlag){
            if(strcmp(cpu->stage[MEM].opcode, "LOAD") == 0){
              stage->rs2_value = cpu->data_memory[cpu->stage[MEM].mem_address];
              stage->stalled = 0;
              stage->stallDueToLoadFlag = 0;
              valueFilled = 1;
            } else{
              stage->stalled=1;
            }
          } else{
            stage->rs2_value = cpu->stage[EX].buffer;
            if(strcmp(cpu->stage[EX].opcode, "LOAD") == 0){
              stage->stalled = 1;
              stage->stallDueToLoadFlag = 1;
              valueFilled = 0;
            }else{
              valueFilled = 1;
            }
          }
        }

          /**If dependency is due to instruction from stage EX then take its buffer value*/
        else if (cpu->stage[MEM].rd == stage->rs2) {
          valueFilled = 1;
          stage->rs2_value = cpu->stage[MEM].buffer;
        }

          /**If dependency is due to instruction from stage EX then take its buffer value*/
        else if (cpu->stage[WB].rd == stage->rs2) {
          stage->rs2_value = cpu->stage[WB].buffer;
          valueFilled = 1;
        }
      }

      if (!valueFilled) {
        stage->stalled = 1;
      }
    }

    /* Copy data from decode latch to execute latch*/
    if (!cpu->stage[EX].stalled && !stage->stalled) {
      cpu->stage[EX] = cpu->stage[DRF];
    }

    if (!stage->stalled && cpu->stage[EX].stalled) {
      stage->stalled = 1;
      stage->stallDueToNextStage = 1;
    }

    /*if (ENABLE_DEBUG_MESSAGES) {
      print_stage_content("Decode/RF", stage);
    }*/
  }

  if (ENABLE_DEBUG_MESSAGES) {
    print_stage_content("Decode/RF", stage);
  }
  return 0;
}

/**
 * Execute Stage of APEX Pipeline
 * @param cpu
 * @return
 */
int execute(APEX_CPU *cpu) {
  CPU_Stage *stage = &cpu->stage[EX];


  /*Check stall status due to MUL instruction*/
  int justRemovedMULStall = 0;
  if (stage->stalled && strcmp(stage->opcode, "MUL") == 0) {
    stage->stalled = 0;
    justRemovedMULStall = 1;
  }

  if (!stage->busy && !stage->stalled) {

    /* Store */
    if (strcmp(stage->opcode, "STORE") == 0) {
      //calculate (src2 + literal)
      stage->mem_address = stage->rs2_value + stage->imm;

      if (cpu->stage[MEM].rd == stage->rs1) {
        stage->rs1_value = cpu->stage[MEM].buffer;
      }

      //If dependency is due to instruction from stage EX then take its buffer value
      else if (cpu->stage[WB].rd == stage->rs1) {
        stage->rs1_value = cpu->stage[WB].buffer;
      }
    }

    /* LOAD */
    if (strcmp(stage->opcode, "LOAD") == 0) {
      //calculate (src2 + literal)
      stage->mem_address = stage->rs1_value + stage->imm;
    }

    /* MOVC */
    if (strcmp(stage->opcode, "MOVC") == 0) {
      stage->buffer = stage->imm;
    }

    /* ADD */
    if (strcmp(stage->opcode, "ADD") == 0) {
      stage->buffer = stage->rs1_value + stage->rs2_value;
    }

    /* SUB */
    if (strcmp(stage->opcode, "SUB") == 0) {
      stage->buffer = stage->rs1_value - stage->rs2_value;
    }

    /* MUL */
    if (strcmp(stage->opcode, "MUL") == 0) {
      stage->buffer = stage->rs1_value * stage->rs2_value;

      if (!justRemovedMULStall) {
        stage->stalled = 1;
      }
    }

    /* AND */
    if (strcmp(stage->opcode, "AND") == 0) {
      stage->buffer = stage->rs1_value & stage->rs2_value;
    }

    /* OR */
    if (strcmp(stage->opcode, "OR") == 0) {
      stage->buffer = stage->rs1_value | stage->rs2_value;
    }

    /* EX-OR */
    if (strcmp(stage->opcode, "EX-OR") == 0) {
      stage->buffer = stage->rs1_value ^ stage->rs2_value;
    }

    /* BZ */
    if (strcmp(stage->opcode, "BZ") == 0) {
      if (DATA_FORWARDING_ENABLED) {
        if (cpu->stage[MEM].buffer == 0) {
          bzBnzBranchHandling(cpu, EX);
        }
      } else {
        if (stage->handleBZInNextStage && stage->zFlag == 0) {
          stage->handleBZInNextStage = 0;
          stage->zFlag = 999;
          bzBnzBranchHandling(cpu, EX);
        }
      }
    }

    /* BNZ */
    if (strcmp(stage->opcode, "BNZ") == 0) {
      if (DATA_FORWARDING_ENABLED) {
        if (cpu->stage[MEM].buffer != 0) {
          bzBnzBranchHandling(cpu, EX);
        }
      } else {
        if (stage->handleBNZInNextStage && stage->zFlag != 0) {
          stage->handleBNZInNextStage = 0;
          stage->zFlag = 999;
          bzBnzBranchHandling(cpu, EX);
        }
      }
    }

    /* JUMP */
    if (strcmp(stage->opcode, "JUMP") == 0) {
      stage->handleJumpInNextStage = 1;
    }

    /* HALT */
    if (strcmp(stage->opcode, "HALT") == 0) {
      flushStageWithEmpty(cpu, F);
      flushStageWithEmpty(cpu, DRF);
      cpu->ins_completed = cpu->code_memory_size - 2;
      cpu->haltFlag = 1;
    }

    int isMeBranchInstr = 0;
    if (strcmp(stage->opcode, "BNZ") == 0 || strcmp(stage->opcode, "BZ") == 0) {
      isMeBranchInstr = 1;
    }

    if (!stage->stalled && !isMeBranchInstr && cpu->stage[DRF].stalled && !cpu->stage[DRF].stallDueToNextStage) {
      flushStageWithNOP(cpu, EX, 1, 0);
    }

    /* Copy data from Execute latch to Memory latch*/
    if (!stage->stalled) {
      cpu->stage[MEM] = cpu->stage[EX];
    }

    /*if (ENABLE_DEBUG_MESSAGES) {
      print_stage_content("Execute", stage);
    }*/
  }

  if (ENABLE_DEBUG_MESSAGES) {
    print_stage_content("Execute", stage);
  }

  return 0;
}

/**
 * Memory Stage of APEX Pipeline
 * @param cpu
 * @return
 */
int memory(APEX_CPU *cpu) {
  CPU_Stage *stage = &cpu->stage[MEM];

  if (!stage->busy && !stage->stalled) {

    /* Store */
    if (strcmp(stage->opcode, "STORE") == 0) {
      cpu->data_memory[stage->mem_address] = stage->rs1_value;
    }

    /* Load */
    if (strcmp(stage->opcode, "LOAD") == 0) {
      stage->buffer = cpu->data_memory[stage->mem_address];
    }

    /* MOVC */
    if (strcmp(stage->opcode, "MOVC") == 0) {
      /** No memory operation in this stage*/
    }

    /* BZ, BNZ, JUMP*/
    if (
        strcmp(stage->opcode, "BZ") == 0 ||
        strcmp(stage->opcode, "JUMP") == 0 ||
        strcmp(stage->opcode, "HALT") == 0 ||
        strcmp(stage->opcode, "BNZ") == 0
        ) {
      /** No memory operation in this stage*/
    }

    /* ADD, SUB, MUL, AND, EX-OR, OR*/
    if (
        strcmp(stage->opcode, "ADD") == 0 ||
        strcmp(stage->opcode, "SUB") == 0 ||
        strcmp(stage->opcode, "MUL") == 0 ||
        strcmp(stage->opcode, "AND") == 0 ||
        strcmp(stage->opcode, "EX-OR") == 0 ||
        strcmp(stage->opcode, "OR") == 0
        ) {
      /** No memory operation in this stage*/
    }

    /*Flush DRF and F stage data to NOP due to branch instruction*/
    if (stage->flushInNextStage) {
      int dontChangeValid = 0;
      flushStageWithNOP(cpu, DRF, dontChangeValid, 0);
      flushStageWithNOP(cpu, EX, dontChangeValid, 0);
      stage->flushInNextStage = 0;
    }

    /* JUMP */
    if (stage->handleJumpInNextStage) {
      cpu->pc = stage->rs1_value + stage->imm;
      cpu->ins_completed = get_code_index(cpu->pc) - 1;

      int dontChangeValid = 0;
      flushStageWithNOP(cpu, DRF, dontChangeValid, 1);
      flushStageWithNOP(cpu, EX, dontChangeValid, 1);

      stage->handleJumpInNextStage = 0;
    }

    if (cpu->stage[EX].stalled) {
      flushStageWithNOP(cpu, MEM, 1, 0);
    }

    /* Copy data from decode latch to execute latch*/
    cpu->stage[WB] = cpu->stage[MEM];

    if (ENABLE_DEBUG_MESSAGES) {
      print_stage_content("Memory", stage);
    }
  }
  return 0;
}

/**
 * Writeback Stage of APEX Pipeline
 * @param cpu
 * @return
 */
int writeback(APEX_CPU *cpu) {
  CPU_Stage *stage = &cpu->stage[WB];
  if (!stage->busy && !stage->stalled) {

    /* Update register file */
    if (
        strcmp(stage->opcode, "MOVC") == 0 ||
        strcmp(stage->opcode, "ADD") == 0 ||
        strcmp(stage->opcode, "SUB") == 0 ||
        strcmp(stage->opcode, "MUL") == 0 ||
        strcmp(stage->opcode, "AND") == 0 ||
        strcmp(stage->opcode, "EX-OR") == 0 ||
        strcmp(stage->opcode, "OR") == 0 ||
        strcmp(stage->opcode, "LOAD") == 0
        ) {
      cpu->regs[stage->rd] = stage->buffer;
    }

    /* Set Z flag for arithmetic instructions*/
    if (
        strcmp(stage->opcode, "ADD") == 0 ||
        strcmp(stage->opcode, "SUB") == 0 ||
        strcmp(stage->opcode, "MUL") == 0
        ) {
      /** If valid instruction, then set result in zFlag*/
      cpu->zFlag = stage->buffer;
    } else {
      /** otherwise set garbage result in zFlag*/
      cpu->zFlag = 999;
    }

    if(!(stage->rd == cpu->stage[DRF].rd || stage->rd == cpu->stage[EX].rd)){
      cpu->regs_valid[stage->rd] = 1;
    }

    if (strcmp(stage->opcode, "NOP") != 0) {
      cpu->ins_completed++;
    }

    if (ENABLE_DEBUG_MESSAGES) {
      print_stage_content("Writeback", stage);
    }
  }
  return 0;
}

/**
 * APEX CPU simulation loop
 * @param cpu
 * @return
 */
int APEX_cpu_run(APEX_CPU *cpu, const char* functionality, const char* cycleCount) {

  if(strcmp(functionality, "display") == 0){
    ENABLE_DEBUG_MESSAGES = 1;
  }

  int desiredCycleCount = atoi(cycleCount);

  while (1) {

    /* All the instructions committed, so exit */
    if (cpu->ins_completed == cpu->code_memory_size) {
      printf("(apex) >> Simulation Complete\n");
      break;
    }

    if (ENABLE_DEBUG_MESSAGES) {
      printf("--------------------------------\n");
      printf("Clock Cycle #: %d\n", cpu->clock+1);// only display count of Clock cycle is increased.
      printf("--------------------------------\n");
    }

    writeback(cpu);
    memory(cpu);
    execute(cpu);
    decode(cpu);
    fetch(cpu);
    cpu->clock++;

    if(desiredCycleCount == cpu->clock){
      break;
    }
  }

  printf("=========STATE OF ARCHITECTURAL REGISTER FILE============\n");
  for (int i = 0; i < 16; i++) {
    char *validStr = "Valid";
    if (cpu->regs_valid[i] == 999 && i != 0) { //check i!=0
      validStr = "Invalid";
    }
    printf(" |\tREG[%d]\t|\tValue = %-5d\t|\tStatus = %-8s\t|\n", i, cpu->regs[i], validStr);
  }

  printf("\n=========STATE OF DATA MEMORY============\n");
  for (int i = 0; i < 100; i++) {
    printf("\t|\tMRM[%d]\t|\tValue = %d\t|\n", i, cpu->data_memory[i]);
  }

  return 0;
}