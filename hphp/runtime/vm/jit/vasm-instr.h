/*
   +----------------------------------------------------------------------+
   | HipHop for PHP                                                       |
   +----------------------------------------------------------------------+
   | Copyright (c) 2010-2016 Facebook, Inc. (http://www.facebook.com)     |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
*/

#ifndef incl_HPHP_JIT_VASM_INSTR_H_
#define incl_HPHP_JIT_VASM_INSTR_H_

#include "hphp/runtime/vm/jit/types.h"
#include "hphp/runtime/vm/jit/arg-group.h"
#include "hphp/runtime/vm/jit/call-spec.h"
#include "hphp/runtime/vm/jit/fixup.h"
#include "hphp/runtime/vm/jit/phys-reg.h"
#include "hphp/runtime/vm/jit/service-requests.h"
#include "hphp/runtime/vm/jit/vasm.h"
#include "hphp/runtime/vm/jit/vasm-data.h"
#include "hphp/runtime/vm/jit/vasm-reg.h"
#include "hphp/runtime/vm/jit/stack-offsets.h"
#include "hphp/runtime/vm/srckey.h"

#include "hphp/vixl/a64/constants-a64.h"

#include "hphp/util/asm-x64.h"
#include "hphp/util/data-block.h"
#include "hphp/util/immed.h"

namespace HPHP { namespace jit {
///////////////////////////////////////////////////////////////////////////////

struct IRInstruction;
struct Vunit;

///////////////////////////////////////////////////////////////////////////////

/*
 * Field actions:
 *
 *    I(f)      immediate
 *    Inone     no immediates
 *    U(s)      use s
 *    UA(s)     use s, but s lifetime extends across the instruction
 *    UH(s,h)   use s, try assigning same register as h
 *    D(d)      define d
 *    DH(d,h)   define d, try assigning same register as h
 *    Un,Dn     no uses, defs
 */
#define VASM_OPCODES\
  /* service requests */\
  O(bindjmp, I(target) I(spOff) I(trflags), U(args), Dn)\
  O(bindjcc, I(cc) I(target) I(spOff) I(trflags), U(sf) U(args), Dn)\
  O(bindjcc1st, I(cc) I(targets[0]) I(targets[1]) I(spOff), U(sf) U(args), Dn)\
  O(bindaddr, I(addr) I(target) I(spOff), Un, Dn)\
  O(fallback, I(target) I(spOff) I(trflags), U(args), Dn)\
  O(fallbackcc, I(cc) I(target) I(spOff) I(trflags), U(sf) U(args), Dn)\
  O(retransopt, I(transID) I(target) I(spOff), U(args), Dn)\
  /* vasm intrinsics */\
  O(copy, Inone, UH(s,d), DH(d,s))\
  O(copy2, Inone, UH(s0,d0) UH(s1,d1), DH(d0,s0) DH(d1,s1))\
  O(copyargs, Inone, UH(s,d), DH(d,s))\
  O(debugtrap, Inone, Un, Dn)\
  O(fallthru, Inone, U(args), Dn)\
  O(ldimmb, I(s), Un, D(d))\
  O(ldimmw, I(s), Un, D(d))\
  O(ldimml, I(s), Un, D(d))\
  O(ldimmq, I(s), Un, D(d))\
  O(ldimmqs, I(s), Un, D(d))\
  O(load, Inone, U(s), D(d))\
  O(store, Inone, U(s) U(d), Dn)\
  O(mcprep, Inone, Un, D(d))\
  O(phidef, Inone, Un, D(defs))\
  O(phijcc, I(cc), U(uses) U(sf), Dn)\
  O(phijmp, Inone, U(uses), Dn)\
  O(conjure, Inone, Un, D(c))\
  O(conjureuse, Inone, U(c), Dn)\
  /* native function abi */\
  O(vcall, I(call) I(destType) I(fixup), U(args), D(d))\
  O(vinvoke, I(call) I(destType) I(fixup), U(args), D(d))\
  O(call, I(target), U(args), Dn)\
  O(callm, Inone, U(target) U(args), Dn)\
  O(callr, Inone, U(target) U(args), Dn)\
  O(calls, I(target), U(args), Dn)\
  O(ret, Inone, U(args), Dn)\
  /* stub function abi */\
  O(stublogue, Inone, Un, Dn)\
  O(stubret, Inone, U(args), Dn)\
  O(callstub, I(target), U(args), Dn)\
  O(callfaststub, I(fix), U(args), Dn)\
  O(tailcallstub, I(target), U(args), Dn)\
  /* php function abi */\
  O(defvmsp, Inone, Un, D(d))\
  O(syncvmsp, Inone, U(s), Dn)\
  O(defvmret, Inone, Un, D(data) D(type))\
  O(syncvmret, Inone, U(data) U(type), Dn)\
  O(phplogue, Inone, U(fp), Dn)\
  O(stubtophp, Inone, Un, Dn)\
  O(loadstubret, Inone, Un, D(d))\
  O(phpret, Inone, U(fp) U(args), D(d))\
  O(callphp, I(stub), U(args), Dn)\
  O(tailcallphp, Inone, U(target) U(fp) U(args), Dn)\
  O(callarray, I(target), U(args), Dn)\
  O(vcallarray, I(target), U(args) U(extraArgs), Dn)\
  O(contenter, Inone, U(fp) U(target) U(args), Dn)\
  /* vm entry intrinsics */\
  O(calltc, Inone, U(target) U(fp) U(args), Dn)\
  O(resumetc, Inone, U(target) U(args), Dn)\
  O(leavetc, Inone, U(args), Dn)\
  /* exception intrinsics */\
  O(landingpad, I(fromPHPCall), Un, Dn)\
  O(nothrow, Inone, Un, Dn)\
  O(syncpoint, I(fix), Un, Dn)\
  O(unwind, Inone, Un, Dn)\
  /* arithmetic intrinsics */\
  O(absdbl, Inone, U(s), D(d))\
  O(srem, Inone, U(s0) U(s1), D(d))\
  O(divint, Inone, U(s0) U(s1), D(d))\
  /* nop and trap */\
  O(nop, Inone, Un, Dn)\
  O(ud2, Inone, Un, Dn)\
  /* arithmetic instructions */\
  O(addl, Inone, U(s0) U(s1), D(d) D(sf)) \
  O(addli, I(s0), UH(s1,d), DH(d,s1) D(sf)) \
  O(addlm, Inone, U(s0) U(m), D(sf)) \
  O(addlim, I(s0), U(m), D(sf)) \
  O(addq, Inone, U(s0) U(s1), D(d) D(sf)) \
  O(addqi, I(s0), UH(s1,d), DH(d,s1) D(sf)) \
  O(addqim, I(s0), U(m), D(sf)) \
  O(addsd, Inone, U(s0) U(s1), D(d))\
  O(andb, Inone, U(s0) U(s1), D(d) D(sf)) \
  O(andbi, I(s0), UH(s1,d), DH(d,s1) D(sf)) \
  O(andbim, I(s), U(m), D(sf)) \
  O(andl, Inone, U(s0) U(s1), D(d) D(sf)) \
  O(andli, I(s0), UH(s1,d), DH(d,s1) D(sf)) \
  O(andq, Inone, U(s0) U(s1), D(d) D(sf)) \
  O(andqi, I(s0), UH(s1,d), DH(d,s1) D(sf)) \
  O(decl, Inone, UH(s,d), DH(d,s) D(sf))\
  O(declm, Inone, U(m), D(sf))\
  O(decq, Inone, UH(s,d), DH(d,s) D(sf))\
  O(decqm, Inone, U(m), D(sf))\
  O(decqmlock, Inone, U(m), D(sf))\
  O(incw, Inone, UH(s,d), DH(d,s) D(sf))\
  O(incwm, Inone, U(m), D(sf))\
  O(incl, Inone, UH(s,d), DH(d,s) D(sf))\
  O(inclm, Inone, U(m), D(sf))\
  O(incq, Inone, UH(s,d), DH(d,s) D(sf))\
  O(incqm, Inone, U(m), D(sf))\
  O(incqmlock, Inone, U(m), D(sf))\
  O(imul, Inone, U(s0) U(s1), D(d) D(sf))\
  O(neg, Inone, UH(s,d), DH(d,s) D(sf))\
  O(notb, Inone, UH(s,d), DH(d,s))\
  O(not, Inone, UH(s,d), DH(d,s))\
  O(orbim, I(s0), U(m), D(sf))\
  O(orwim, I(s0), U(m), D(sf))\
  O(orq, Inone, U(s0) U(s1), D(d) D(sf))\
  O(orqi, I(s0), UH(s1,d), DH(d,s1) D(sf)) \
  O(orqim, I(s0), U(m), D(sf))\
  O(sar, Inone, U(s0) U(s1), D(d) D(sf))\
  O(shl, Inone, U(s0) U(s1), D(d) D(sf))\
  O(sarqi, I(s0), UH(s1,d), DH(d,s1) D(sf))\
  O(shlli, I(s0), UH(s1,d), DH(d,s1) D(sf))\
  O(shlqi, I(s0), UH(s1,d), DH(d,s1) D(sf))\
  O(shrli, I(s0), UH(s1,d), DH(d,s1) D(sf))\
  O(shrqi, I(s0), UH(s1,d), DH(d,s1) D(sf))\
  O(psllq, I(s0), UH(s1,d), DH(d,s1))\
  O(psrlq, I(s0), UH(s1,d), DH(d,s1))\
  O(subbi, I(s0), UH(s1,d), DH(d,s1) D(sf))\
  O(subl, Inone, UA(s0) U(s1), D(d) D(sf))\
  O(subli, I(s0), UH(s1,d), DH(d,s1) D(sf))\
  O(subq, Inone, UA(s0) U(s1), D(d) D(sf))\
  O(subqi, I(s0), UH(s1,d), DH(d,s1) D(sf))\
  O(subsd, Inone, UA(s0) U(s1), D(d))\
  O(xorb, Inone, U(s0) U(s1), D(d) D(sf))\
  O(xorbi, I(s0), UH(s1,d), DH(d,s1) D(sf))\
  O(xorl, Inone, U(s0) U(s1), D(d) D(sf))\
  O(xorq, Inone, U(s0) U(s1), D(d) D(sf))\
  O(xorqi, I(s0), UH(s1,d), DH(d,s1) D(sf))\
  /* compares and tests */\
  O(cmpb, Inone, U(s0) U(s1), D(sf))\
  O(cmpbi, I(s0), U(s1), D(sf))\
  O(cmpbim, I(s0), U(s1), D(sf))\
  O(cmpwim, I(s0), U(s1), D(sf))\
  O(cmpl, Inone, U(s0) U(s1), D(sf))\
  O(cmpli, I(s0), U(s1), D(sf))\
  O(cmplm, Inone, U(s0) U(s1), D(sf))\
  O(cmplim, I(s0), U(s1), D(sf))\
  O(cmpq, Inone, U(s0) U(s1), D(sf))\
  O(cmpqi, I(s0), U(s1), D(sf))\
  O(cmpqm, Inone, U(s0) U(s1), D(sf))\
  O(cmpqim, I(s0), U(s1), D(sf))\
  O(cmpsd, I(pred), UA(s0) U(s1), D(d))\
  O(ucomisd, Inone, U(s0) U(s1), D(sf))\
  O(testb, Inone, U(s0) U(s1), D(sf))\
  O(testbi, I(s0), U(s1), D(sf))\
  O(testbim, I(s0), U(s1), D(sf))\
  O(testwim, I(s0), U(s1), D(sf))\
  O(testl, Inone, U(s0) U(s1), D(sf))\
  O(testli, I(s0), U(s1), D(sf))\
  O(testlim, I(s0), U(s1), D(sf))\
  O(testq, Inone, U(s0) U(s1), D(sf))\
  O(testqi, I(s0), U(s1), D(sf))\
  O(testqm, Inone, U(s0) U(s1), D(sf))\
  O(testqim, I(s0), U(s1), D(sf))\
  /* conditional operations */\
  O(cloadq, I(cc), U(sf) U(f) U(t), D(d))\
  O(cmovb, I(cc), U(sf) U(f) U(t), D(d))\
  O(cmovq, I(cc), U(sf) U(f) U(t), D(d))\
  O(setcc, I(cc), U(sf), D(d))\
  /* load effective address */\
  O(lea, Inone, U(s), D(d))\
  O(leap, I(s), Un, D(d))\
  O(lead, I(s), Un, D(d))\
  /* copies */\
  O(movb, Inone, UH(s,d), DH(d,s))\
  O(movl, Inone, UH(s,d), DH(d,s))\
  O(movzbl, Inone, UH(s,d), DH(d,s))\
  O(movzbq, Inone, UH(s,d), DH(d,s))\
  O(movzlq, Inone, UH(s,d), DH(d,s))\
  O(movtqb, Inone, UH(s,d), DH(d,s))\
  O(movtql, Inone, UH(s,d), DH(d,s))\
  /* loads/stores */\
  O(loadb, Inone, U(s), D(d))\
  O(loadw, Inone, U(s), D(d))\
  O(loadl, Inone, U(s), D(d))\
  O(loadqp, I(s), Un, D(d))\
  O(loadqd, I(s), Un, D(d))\
  O(loadups, Inone, U(s), D(d))\
  O(loadsd, Inone, U(s), D(d))\
  O(loadzbl, Inone, U(s), D(d))\
  O(loadzbq, Inone, U(s), D(d))\
  O(loadzlq, Inone, U(s), D(d))\
  O(loadtqb, Inone, U(s), D(d))\
  O(storeb, Inone, U(s) U(m), Dn)\
  O(storebi, I(s), U(m), Dn)\
  O(storew, Inone, U(s) U(m), Dn)\
  O(storewi, I(s), U(m), Dn)\
  O(storel, Inone, U(s) U(m), Dn)\
  O(storeli, I(s), U(m), Dn)\
  O(storeqi, I(s), U(m), Dn)\
  O(storeups, Inone, U(s) U(m), Dn)\
  O(storesd, Inone, U(s) U(m), Dn)\
  /* branches */\
  O(jcc, I(cc), U(sf), Dn)\
  O(jcci, I(cc), U(sf), Dn)\
  O(jmp, Inone, Un, Dn)\
  O(jmpr, Inone, U(target) U(args), Dn)\
  O(jmpm, Inone, U(target) U(args), Dn)\
  O(jmpi, I(target), U(args), Dn)\
  /* push/pop */\
  O(pop, Inone, Un, D(d))\
  O(popm, Inone, U(d), Dn)\
  O(push, Inone, U(s), Dn)\
  O(pushm, Inone, U(s), Dn)\
  /* floating-point conversions */\
  O(cvttsd2siq, Inone, U(s), D(d))\
  O(cvtsi2sd, Inone, U(s), D(d))\
  O(cvtsi2sdm, Inone, U(s), D(d))\
  O(unpcklpd, Inone, UA(s0) U(s1), D(d))\
  /* other floating-point */\
  O(divsd, Inone, UA(s0) U(s1), D(d))\
  O(mulsd, Inone, U(s0) U(s1), D(d))\
  O(roundsd, I(dir), U(s), D(d))\
  O(sqrtsd, Inone, U(s), D(d))\
  /* x64 instructions */\
  O(cqo, Inone, Un, Dn)\
  O(idiv, Inone, U(s), D(sf))\
  O(sarq, Inone, UH(s,d), DH(d,s) D(sf))\
  O(shlq, Inone, UH(s,d), DH(d,s) D(sf))\
  /* arm instructions */\
  O(brk, I(code), Un, Dn)\
  O(cbcc, I(cc), U(s), Dn)\
  O(hostcall, I(argc), U(args), Dn)\
  O(tbcc, I(cc) I(bit), U(s), Dn)\
  /* ppc64 instructions */\
  O(extsb, Inone, UH(s,d), DH(d,s) D(sf))\
  O(extsw, Inone, UH(s,d), DH(d,s) D(sf))\
  O(fcmpo, Inone, U(s0) U(s1), D(sf))\
  O(fcmpu, Inone, U(s0) U(s1), D(sf))\
  O(xscvdpsxds, Inone, U(s), D(d))\
  O(mfcr, Inone, Un, D(d))\
  O(mflr, Inone, Un, D(d))\
  O(mfvsrd, Inone, U(s), D(d))\
  O(movlk, Inone, UH(s,d), DH(d,s))\
  O(mtlr, Inone, U(s), Dn)\
  O(mtvsrd, Inone, U(s), D(d))\
  O(xscvsxddp, Inone, U(s), D(d))\
  O(xxlxor, Inone, U(s0) U(s1), D(d))\
  O(xxpermdi, Inone, U(s0) U(s1), D(d))\
  /* */

/*
 * ATT style operand order.  For binary ops:
 *    op   s0 s1 d:  d = s1 op s0    =>   d=s1; d op= s0
 *    op   imm s1 d: d = s1 op imm   =>   d=s1; d op= imm
 *    cmp  s0 s1:    s1 cmp s0
 */

/*
 * Suffix conventions:
 *    b   8-bit
 *    w   16-bit
 *    l   32-bit
 *    q   64-bit
 *    sd  double
 *    i   immediate
 *    m   Vptr
 *    p   RIPRelativeRef
 *    d   VdataPtr
 *    s   smashable
 */

///////////////////////////////////////////////////////////////////////////////
// Service requests.

struct bindjmp {
  explicit bindjmp(SrcKey target,
                   FPInvOffset spOff,
                   TransFlags trflags,
                   RegSet args)
    : target{target}
    , spOff(spOff)
    , trflags{trflags}
    , args{args}
  {}

  SrcKey target;
  FPInvOffset spOff;
  TransFlags trflags;
  RegSet args;
};

struct bindjcc {
  explicit bindjcc(ConditionCode cc,
                   VregSF sf,
                   SrcKey target,
                   FPInvOffset spOff,
                   TransFlags trflags,
                   RegSet args)
    : cc{cc}
    , sf{sf}
    , target{target}
    , spOff(spOff)
    , trflags{trflags}
    , args{args}
  {}

  ConditionCode cc;
  VregSF sf;
  SrcKey target;
  FPInvOffset spOff;
  TransFlags trflags;
  RegSet args;
};

struct bindjcc1st {
  explicit bindjcc1st(ConditionCode cc,
                      VregSF sf,
                      std::array<SrcKey,2> targets,
                      FPInvOffset spOff,
                      RegSet args)
    : cc{cc}
    , sf{sf}
    , spOff(spOff)
    , args{args}
  {
    this->targets[0] = targets[0];
    this->targets[1] = targets[1];
  }

  ConditionCode cc;
  VregSF sf;
  SrcKey targets[2];
  FPInvOffset spOff;
  RegSet args;
};

struct bindaddr {
  explicit bindaddr(VdataPtr<TCA> addr, SrcKey target, FPInvOffset spOff)
    : addr(addr)
    , target(target)
    , spOff(spOff)
  {}

  VdataPtr<TCA> addr;
  SrcKey target;
  FPInvOffset spOff;
};

struct fallback {
  explicit fallback(SrcKey target,
                    FPInvOffset spOff,
                    TransFlags trflags,
                    RegSet args)
    : target{target}
    , spOff(spOff)
    , trflags{trflags}
    , args{args}
  {}

  SrcKey target;
  FPInvOffset spOff;
  TransFlags trflags;
  RegSet args;
};

struct fallbackcc {
  explicit fallbackcc(ConditionCode cc,
                      VregSF sf,
                      SrcKey target,
                      FPInvOffset spOff,
                      TransFlags trflags,
                      RegSet args)
    : cc{cc}
    , sf{sf}
    , target{target}
    , spOff(spOff)
    , trflags{trflags}
    , args{args}
  {}

  ConditionCode cc;
  VregSF sf;
  SrcKey target;
  FPInvOffset spOff;
  TransFlags trflags;
  RegSet args;
};

struct retransopt {
  explicit retransopt(TransID transID,
                      SrcKey target,
                      FPInvOffset spOff,
                      RegSet args)
    : transID{transID}
    , target{target}
    , spOff(spOff)
    , args{args}
  {}

  TransID transID;
  SrcKey target;
  FPInvOffset spOff;
  RegSet args;
};

///////////////////////////////////////////////////////////////////////////////
// VASM intrinsics.

/*
 * Copies of different arities.
 *
 * All copies happen in parallel, meaning operand order doesn't matter when a
 * PhysReg appears as both a src and dst.
 */
struct copy { Vreg s, d; };
struct copy2 { Vreg64 s0, s1, d0, d1; };
struct copyargs { Vtuple s, d; };

/*
 * Cause any attached debugger to trap.
 *
 * Process may abort if no debugger is attached.
 */
struct debugtrap {};

/*
 * No-op.
 *
 * Used for marking the end of a block that is intentionally going to fall
 * through.  Only for use with Vauto.
 */
struct fallthru { RegSet args; };

/*
 * Load an immedate value without mutating status flags.
 */
struct ldimmb { Immed s; Vreg d; };
struct ldimmw { Immed s; Vreg16 d; };
struct ldimml { Immed s; Vreg d; };
struct ldimmq { Immed64 s; Vreg d; };
struct ldimmqs { Immed64 s; Vreg d; };

/*
 * Memory operand load and store.
 */
struct load { Vptr s; Vreg d; };
struct store { Vreg s; Vptr d; };

/*
 * Method cache smashable prime data.
 *
 * @see: cgLdObjMethod()
 */
struct mcprep { Vreg64 d; };

/*
 * Phis.
 *
 * @see: doWhile(), for an example usage.
 */
struct phidef { Vtuple defs; };
struct phijmp { Vlabel target; Vtuple uses; };
struct phijcc { ConditionCode cc; VregSF sf; Vlabel targets[2]; Vtuple uses; };

/*
 * These marker instructions are used to model dataflow in pseudo-translations.
 * They should not be used in any translations that will eventually be emitted.
 */
struct conjure { Vreg c; };
struct conjureuse { Vreg c; };

///////////////////////////////////////////////////////////////////////////////
// Native function ABI.

/*
 * Native or stub function call, without or with exception edges, respectively.
 *
 * Contains information about a helper call (i.e., any non-PHP function call)
 * needed for lowering to different target architectures.
 */
struct vcall { CallSpec call; VcallArgsId args; Vtuple d;
               Fixup fixup; DestType destType; bool nothrow; };
struct vinvoke { CallSpec call; VcallArgsId args; Vtuple d; Vlabel targets[2];
                 Fixup fixup; DestType destType; };

/*
 * C++ function call using the native ABI.
 *
 * Comes in five flavors:
 *    call:  direct call
 *    callm: indirect call via memory operand
 *    callr: indirect call via register
 *    calls: direct call with smashable target
 *
 * (These follow the same suffix conventions described above.)
 *
 * If `watch' is set, *watch will be set to the address immediately following
 * the call instruction---useful for various unwinder hijinks.
 */
struct call  { CodeAddress target; RegSet args; TCA* watch; };
struct callm { Vptr target; RegSet args; };
struct callr { Vreg64 target; RegSet args; };
struct calls { CodeAddress target; RegSet args; };

/*
 * Native function return.
 */
struct ret { RegSet args; };

///////////////////////////////////////////////////////////////////////////////
// Stub function ABI.

/*
 * Stub function prologue.
 *
 * Set up the stub call frame---which has the same layout as the native call
 * frame (and hence is platform dependent), except not all components are
 * required to be valid.
 *
 * On x64, for example, this leaves the native stack as follows:
 *
 *    +-----------------------+   <- native stack pointer, pre-call
 *    |     return address    |
 *    +-----------------------+
 *    | <junk> or saved rvmfp |
 *    +-----------------------+   <- native stack pointer, after stublogue{}
 *
 * The return address is always required, but the frame pointer is only
 * optionally saved, when `saveframe' is set because the stub (or some native
 * helper that it calls) needs to use it.
 *
 * Stubs are not allowed to spill registers to the stack, so the native stack
 * pointer will only be adjusted if the stub code does so intentionally.
 */
struct stublogue { bool saveframe; };

/*
 * Return from a stub.
 *
 * Return to the address saved on the stack, and restore the native stack
 * pointer to wherever it was before the stub call.  See the diagram for
 * stublogue{}, above; `saveframe' has the same meaning as it does there.
 */
struct stubret { RegSet args; bool saveframe; };

/*
 * Direct call to the unique stub at `target'.
 *
 * The `target' should begin with a stublogue{} instruction, which together
 * with callstub{} should implement the ABI described above.
 */
struct callstub { CodeAddress target; RegSet args; };

/*
 * Call a "fast" stub, a stub that preserves more registers than a normal call.
 *
 * It may still call C++ functions on a slow path (which is why there's a Fixup
 * operand) but it will save any required registers before doing so.
 */
struct callfaststub { TCA target; Fixup fix; RegSet args; };

/*
 * Make a direct tail call to a stub.
 *
 * As in the usual sense of tail call, this is really a jmp which will cause
 * the callee's return to serve as the caller's return.
 *
 * This instruction jumps from a context dominated by stublogue{} to a context
 * which wants to execute a logically identical prologue, so it needs to revert
 * the world to a pre-stublogue{} state before jumping.
 */
struct tailcallstub { CodeAddress target; RegSet args; };

///////////////////////////////////////////////////////////////////////////////
// PHP function ABI.

/*
 * Copy rvmsp() into `d'.
 *
 * Used once per region when reentering a resumed function after an ABI
 * boundary.
 */
struct defvmsp { Vreg d; };

/*
 * Copy `s' into rvmsp().
 *
 * Used right before leaving translated code for an ABI boundary, such as
 * bindjmp{} or fallbackcc{}.
 */
struct syncvmsp { Vreg s; };

/*
 * Copy the PHP return value from the return registers into `data' and `type'.
 *
 * Used right after an instruction that makes a PHP call (like the
 * suggestively-named callphp{}) to receive the values as Vregs.
 */
struct defvmret { Vreg data; Vreg type; };

/*
 * Copy a PHP return value into the return registers (rreg(0) and rreg(1)).
 *
 * This should be used right before we execute a phpret{}.
 */
struct syncvmret { Vreg data; Vreg type; };

/*
 * PHP function prologue.
 *
 * Save the return instruction pointer in m_savedRip on the current VM frame,
 * `fp', and ensure that the native stack pointer is in the same position as it
 * was before the instruction that transferred control to us.
 *
 * The phplogue should dominate all code that is logically part of a PHP func
 * prologue or func body (but /not/ the func guard, which precedes it).  Note
 * that this includes unique stubs like fcallHelperThunk, which are reached by
 * PHP function call.
 *
 * Ultimately, anytime we hit a phplogue, we came from enterTCHelper, which
 * means that after the phplogue (since we maintain the native stack pointer),
 * the stack looks like this:
 *
 *    +-----------------------+
 *    |  <enterTCHelper+???>  |
 *    +-----------------------+   <- native stack pointer
 *
 * i.e., the return address in enterTCHelper of the call that put us in the TC.
 * The native stack continues to point here as long as we are in the TC, modulo
 * register allocator spill space.
 */
struct phplogue { Vreg fp; };

/*
 * Convert from a stublogue{} context to a phplogue{} context.
 *
 * This is only used by fcallArrayHelper, which needs to begin with a
 * stublogue{} (see unique-stubs.cpp) and later perform the work of phplogue{}.
 *
 * This does not modify the PHP ActRec, which can be loaded prior to this
 * instruction using loadstubret.
 */
struct stubtophp {};

/*
 * Load the return address for this stub in rvmfp(). This is only valid from a
 * stublogue{} context.
 *
 * This is only used by fcallArrayHelper, which needs to begin with a
 * stublogue{} (see unique-stubs.cpp) and later perform the work of phplogue{}.
 */
struct loadstubret { Vreg d; };

/*
 * Load fp[m_sfp] into `d' and return to m_savedRip on `fp'.
 *
 * If `noframe' is set, `d' is not changed.
 */
struct phpret { Vreg fp; Vreg d; RegSet args; bool noframe; };

/*
 * Call a PHP function.
 *
 * This is a smashable call that begins its life as a request to translate the
 * callee, and winds up as a direct call to the callee's func guard or
 * prologue.
 */
struct callphp {
  explicit callphp(TCA stub,
                   RegSet args,
                   std::array<Vlabel,2> targets)
    : stub{stub}
    , args{args}
  {
    this->targets[0] = targets[0];
    this->targets[1] = targets[1];
  }

  TCA stub;
  RegSet args;
  Vlabel targets[2];
};

/*
 * Make an indirect tail call to a PHP function.
 *
 * Analogous to tailcallstub{}; undoes phplogue{} and then jumps to `target',
 * which begins with a logically identical phplogue{}.
 */
struct tailcallphp { Vreg target; Vreg fp; RegSet args; };

/*
 * Non-smashable PHP function call with (almost) the same ABI as callphp{}.
 *
 * NB: The only difference is that callarray preserves vmfp.  Currently only
 * used by the CallArray instruction.
 */
struct callarray { TCA target; RegSet args; };

/*
 * High-level version of callarray.
 *
 * Has exception edges and additional integer args (used by the `target' stub).
 */
struct vcallarray { TCA target; RegSet args; Vtuple extraArgs;
                    Vlabel targets[2]; };

/*
 * Enter a continuation (with exception edges).
 *
 * `fp' is the continuation's frame pointer, and `target' is the code address
 * at which to resume execution.
 *
 * Since `target' is dominated by a phplogue{}, we must implement to its ABI.
 * For most architectures, this will probably require calling a small stub
 * function.
 */
struct contenter { Vreg64 fp, target; RegSet args; Vlabel targets[2]; };

///////////////////////////////////////////////////////////////////////////////
// VM entry ABI.

/*
 * Call into the TC at a function prologue.
 *
 * This sets up for a phplogue{} and transfers control to `target'---which
 * logically executes said phplogue{} before doing anything else.
 *
 * Before calltc{} is executed, the stack will always be set up like this:
 *
 *    +-----------------------+   <- 16-byte alignment
 *    |   <8 bytes of junk>   |
 *    +-----------------------+   <- native stack pointer
 *
 * i.e., the native stack pointer will be misaligned coming in (but must, of
 * course, be aliged once phplogue{} finishes executing).  Using a native call
 * in the implementation (and likewise, using native returns for leavetc{}) is
 * recommended, in order to take advantage of return branch predictions.
 *
 * `fp' is the callee's ActRec, which will have already been set up
 * appropriately.  `exittc' is the address to resume execution at after
 * returning from the TC.
 */
struct calltc { Vreg64 target, fp; TCA exittc; RegSet args; };

/*
 * Resume execution in the middle of a TC function.
 *
 * This must set up the native stack in the same way as a phplogue{} would,
 * before transferring control to `target'.  As with calltc{}, the native stack
 * pointer is misaligned coming in, and using a native call is recommended.
 *
 * `exittc' is the address to resume execution at after returning from the TC.
 */
struct resumetc { Vreg64 target; TCA exittc; RegSet args; };

/*
 * Pop the address of enterTCExit off the stack, and return to it.
 *
 * Used to relinquish control to the async scheduler from an async function.
 */
struct leavetc { RegSet args; };

///////////////////////////////////////////////////////////////////////////////
// Exception intrinsics.

/*
 * Header for catch blocks.
 */
struct landingpad { bool fromPHPCall; };

/*
 * Register a null catch trace at this position.
 *
 * This tells the unwinder that the function call returning to here isn't
 * allowed to throw.
 */
struct nothrow {};

/*
 * Register a fixup at this position.
 *
 * These are used by the unwinder to reconstruct the state of the VM registers.
 */
struct syncpoint { Fixup fix; };

/*
 * Terminate a block after a call that can throw, with edges to a catch block
 * and a fallthrough block.
 */
struct unwind { Vlabel targets[2]; };

///////////////////////////////////////////////////////////////////////////////
// Arithmetic intrinsics.

/*
 * Absolute value for a double-precision value.
 */
struct absdbl { Vreg s, d; };

/*
 * Modulus of two integers.
 */
struct srem { Vreg s0, s1, d; };

/*
 * Integer division.
 */
struct divint { Vreg s0, s1, d; };

///////////////////////////////////////////////////////////////////////////////

/*
 * Unless specifically noted otherwise, instructions with Vreg{8,16,32} dsts
 * can do whatever they please with the upper bits.
 */

/*
 * Nop and trap.
 */
struct nop {};
struct ud2 {};

/*
 * Arithmetic instructions.
 */
// add: s0 + {s1|m} => {d|m}, sf
struct addl   { Vreg32 s0, s1, d; VregSF sf; };
struct addli  { Immed s0; Vreg32 s1, d; VregSF sf; };
struct addlm  { Vreg32 s0; Vptr m; VregSF sf; };
struct addlim { Immed s0; Vptr m; VregSF sf; };
struct addq  { Vreg64 s0, s1, d; VregSF sf; };
struct addqi { Immed s0; Vreg64 s1, d; VregSF sf; };
struct addqim { Immed s0; Vptr m; VregSF sf; };
struct addsd  { VregDbl s0, s1, d; };
// and: s0 & {s1|m} => {d|m}, sf
struct andb  { Vreg8 s0, s1, d; VregSF sf; };
struct andbi { Immed s0; Vreg8 s1, d; VregSF sf; };
struct andbim { Immed s; Vptr m; VregSF sf; };
struct andl  { Vreg32 s0, s1, d; VregSF sf; };
struct andli { Immed s0; Vreg32 s1, d; VregSF sf; };
struct andq  { Vreg64 s0, s1, d; VregSF sf; };
struct andqi { Immed s0; Vreg64 s1, d; VregSF sf; };
// dec: {s|m} - 1 => {d|m}, sf
struct decl { Vreg32 s, d; VregSF sf; };
struct declm { Vptr m; VregSF sf; };
struct decq { Vreg64 s, d; VregSF sf; };
struct decqm { Vptr m; VregSF sf; };
struct decqmlock { Vptr m; VregSF sf; };
// inc: {s|m} + 1 => {d|m}, sf
struct incw { Vreg16 s, d; VregSF sf; };
struct incwm { Vptr m; VregSF sf; };
struct incl { Vreg32 s, d; VregSF sf; };
struct inclm { Vptr m; VregSF sf; };
struct incq { Vreg64 s, d; VregSF sf; };
struct incqm { Vptr m; VregSF sf; };
struct incqmlock { Vptr m; VregSF sf; };
// mul: s0 * s1 => d, sf
struct imul { Vreg64 s0, s1, d; VregSF sf; };
// neg: 0 - s => d, sf
struct neg { Vreg64 s, d; VregSF sf; };
// not: ~s => d
struct notb { Vreg8 s, d; };
struct not { Vreg64 s, d; };
// or: s0 | {s1|m} => {d|m}, sf
struct orbim { Immed s0; Vptr m; VregSF sf; };
struct orwim { Immed s0; Vptr m; VregSF sf; };
struct orq { Vreg64 s0, s1, d; VregSF sf; };
struct orqi { Immed s0; Vreg64 s1, d; VregSF sf; };
struct orqim { Immed s0; Vptr m; VregSF sf; };
// shift: s1 << s0 => d, sf
struct sar { Vreg64 s0, s1, d; VregSF sf; };
struct shl { Vreg64 s0, s1, d; VregSF sf; };
struct sarqi { Immed s0; Vreg64 s1, d; VregSF sf; };
struct shlli { Immed s0; Vreg32 s1, d; VregSF sf; };
struct shlqi { Immed s0; Vreg64 s1, d; VregSF sf; };
struct shrli { Immed s0; Vreg32 s1, d; VregSF sf; };
struct shrqi { Immed s0; Vreg64 s1, d; VregSF sf; };
struct psllq { Immed s0; VregDbl s1, d; };
struct psrlq { Immed s0; VregDbl s1, d; };
// sub: s1 - s0 => d, sf
struct subbi { Immed s0; Vreg8 s1, d; VregSF sf; };
struct subl { Vreg32 s0, s1, d; VregSF sf; };
struct subli { Immed s0; Vreg32 s1, d; VregSF sf; };
struct subq { Vreg64 s0, s1, d; VregSF sf; };
struct subqi { Immed s0; Vreg64 s1, d; VregSF sf; };
struct subsd { VregDbl s0, s1, d; };
// xor: s0 ^ s1 => d, sf
struct xorb { Vreg8 s0, s1, d; VregSF sf; };
struct xorbi { Immed s0; Vreg8 s1, d; VregSF sf; };
struct xorl { Vreg32 s0, s1, d; VregSF sf; };
struct xorq { Vreg64 s0, s1, d; VregSF sf; };
struct xorqi { Immed s0; Vreg64 s1, d; VregSF sf; };

/*
 * Compares and tests.
 */
// s1 - s0 => sf
struct cmpb { Vreg8 s0; Vreg8 s1; VregSF sf; };
struct cmpbi { Immed s0; Vreg8 s1; VregSF sf; };
struct cmpbim { Immed s0; Vptr s1; VregSF sf; };
struct cmpwim { Immed s0; Vptr s1; VregSF sf; };
struct cmpl { Vreg32 s0; Vreg32 s1; VregSF sf; };
struct cmpli { Immed s0; Vreg32 s1; VregSF sf; };
struct cmplm { Vreg32 s0; Vptr s1; VregSF sf; };
struct cmplim { Immed s0; Vptr s1; VregSF sf; };
struct cmpq { Vreg64 s0; Vreg64 s1; VregSF sf; };
struct cmpqi { Immed s0; Vreg64 s1; VregSF sf; };
struct cmpqm { Vreg64 s0; Vptr s1; VregSF sf; };
struct cmpqim { Immed s0; Vptr s1; VregSF sf; };
struct cmpsd { ComparisonPred pred; VregDbl s0, s1, d; };
struct ucomisd { VregDbl s0, s1; VregSF sf; };
// s1 & s0 => sf
struct testb { Vreg8 s0, s1; VregSF sf; };
struct testbi { Immed s0; Vreg8 s1; VregSF sf; };
struct testbim { Immed s0; Vptr s1; VregSF sf; };
struct testwim { Immed s0; Vptr s1; VregSF sf; };
struct testl { Vreg32 s0, s1; VregSF sf; };
struct testli { Immed s0; Vreg32 s1; VregSF sf; };
struct testlim { Immed s0; Vptr s1; VregSF sf; };
struct testq { Vreg64 s0, s1; VregSF sf; };
struct testqi { Immed s0; Vreg64 s1; VregSF sf; };
struct testqm { Vreg64 s0; Vptr s1; VregSF sf; };
struct testqim { Immed s0; Vptr s1; VregSF sf; };

/*
 * Conditional operations.
 */
// t1 = load t; d = condition ? t1 : f
struct cloadq { ConditionCode cc; VregSF sf; Vreg64 f; Vptr t; Vreg64 d; };
// d = condition ? t : f
struct cmovb { ConditionCode cc; VregSF sf; Vreg8 f, t, d; };
struct cmovq { ConditionCode cc; VregSF sf; Vreg64 f, t, d; };
// d = condition ? 1 : 0
struct setcc { ConditionCode cc; VregSF sf; Vreg8 d; };

/*
 * Load effective address.
 */
struct lea { Vptr s; Vreg64 d; };
struct leap { RIPRelativeRef s; Vreg64 d; };
struct lead { VdataPtr<void> s; Vreg64 d; };

/*
 * Copies.
 */
// moves
struct movb { Vreg8 s, d; };
struct movl { Vreg32 s, d; };
// zero-extended s to d
struct movzbl { Vreg8 s; Vreg32 d; };
struct movzbq { Vreg8 s; Vreg64 d; };
struct movzlq { Vreg32 s; Vreg64 d; };
// truncated s to d
struct movtqb { Vreg64 s; Vreg8 d; };
struct movtql { Vreg64 s; Vreg32 d; };

/*
 * Loads and stores.
 */
// loads
struct loadb { Vptr s; Vreg8 d; };
struct loadw { Vptr s; Vreg16 d; };
struct loadl { Vptr s; Vreg32 d; };
struct loadqp { RIPRelativeRef s; Vreg64 d; };
struct loadqd { VdataPtr<uint64_t> s; Vreg64 d; };
struct loadups { Vptr s; Vreg128 d; };
struct loadsd { Vptr s; VregDbl d; };
// zero-extended s to d
struct loadzbl { Vptr s; Vreg32 d; };
struct loadzbq { Vptr s; Vreg64 d; };
struct loadzlq { Vptr s; Vreg64 d; };
// truncated s to d
struct loadtqb { Vptr s; Vreg8 d; };
// stores
struct storeb { Vreg8 s; Vptr m; };
struct storebi { Immed s; Vptr m; };
struct storew { Vreg16 s; Vptr m; };
struct storewi { Immed s; Vptr m; };
struct storel { Vreg32 s; Vptr m; };
struct storeli { Immed s; Vptr m; };
struct storeqi { Immed s; Vptr m; };
struct storeups { Vreg128 s; Vptr m; };
struct storesd { VregDbl s; Vptr m; };

/*
 * Branch instructions.
 *
 * In vasm, targets are always ordered {next, taken}.
 */
struct jcc { ConditionCode cc; VregSF sf; Vlabel targets[2]; };
struct jcci { ConditionCode cc; VregSF sf; Vlabel target; TCA taken; };
struct jmp { Vlabel target; };
struct jmpr { Vreg64 target; RegSet args; };
struct jmpm { Vptr target; RegSet args; };
struct jmpi { TCA target; RegSet args; };

/*
 * Push/pop to rsp().
 */
struct pop { Vreg64 d; };
struct popm { Vptr d; };
struct push { Vreg64 s; };
struct pushm { Vptr s; };

/*
 * Integer-float conversions.
 */
struct cvttsd2siq { VregDbl s; Vreg64 d; };
struct cvtsi2sd { Vreg64 s; VregDbl d; };
struct cvtsi2sdm { Vptr s; VregDbl d; };
struct unpcklpd { VregDbl s0, s1; Vreg128 d; };

/*
 * Undocumented floating-point instructions.
 */
struct divsd { VregDbl s0, s1, d; };
struct mulsd  { VregDbl s0, s1, d; };
struct roundsd { RoundDirection dir; VregDbl s, d; };
struct sqrtsd { VregDbl s, d; };

///////////////////////////////////////////////////////////////////////////////

/*
 * x64 intrinsics.
 */
struct cqo {};
struct idiv { Vreg64 s; VregSF sf; };
struct sarq { Vreg64 s, d; VregSF sf; }; // uses rcx
struct shlq { Vreg64 s, d; VregSF sf; }; // uses rcx

/*
 * ARM intrinsics.
 */
struct brk { uint16_t code; };
struct cbcc { vixl::Condition cc; Vreg64 s; Vlabel targets[2]; };
struct tbcc { vixl::Condition cc; unsigned bit; Vreg64 s; Vlabel targets[2]; };
// ARM emulator native call intrinsic.
struct hostcall { RegSet args; uint8_t argc; };

/*
 * ppc64 intrinsics.
 */
struct fcmpo { VregDbl s0; VregDbl s1; VregSF sf; };
struct fcmpu { VregDbl s0; VregDbl s1; VregSF sf; };
struct xscvdpsxds { Vreg128 s, d; };
struct mfcr { Vreg64 d; };
struct mflr { Vreg64 d; };
struct mfvsrd { Vreg128 s; Vreg64 d; };
// move 32bits into a register and keep the higher 32bits
struct movlk { Vreg64 s, d; };
struct mtlr { Vreg64 s; };
struct mtvsrd { Vreg64 s; Vreg128 d; };
struct xscvsxddp { Vreg128 s, d; };
struct xxlxor { Vreg128 s0, s1, d; };
struct xxpermdi { Vreg128 s0, s1, d; };

// Extend byte sign
struct extsb { Vreg64 s; Vreg64 d; VregSF sf; };
struct extsw { Vreg64 s; Vreg64 d; VregSF sf; };

///////////////////////////////////////////////////////////////////////////////

struct Vinstr {
#define O(name, imms, uses, defs) name,
  enum Opcode : uint8_t { VASM_OPCODES };
#undef O

  Vinstr() : op(ud2) {}

#define O(name, imms, uses, defs)               \
  /* implicit */ Vinstr(jit::name i) : op(name), name##_(i) {}
  VASM_OPCODES
#undef O

  /*
   * Define an operator= for all instructions to preserve origin and pos.
   */
#define O(name, ...)                            \
  Vinstr& operator=(const jit::name& i) {       \
    op = Vinstr::name;                          \
    name##_ = i;                                \
    return *this;                               \
  }
  VASM_OPCODES
#undef O

  template<typename Op> struct matcher;
  template<Opcode op> struct op_matcher;

  /*
   * Templated accessors for the union members.
   */
  template<typename Op>
  typename matcher<Op>::type& get() {
    return matcher<Op>::get(*this);
  }
  template<typename Op>
  const typename matcher<Op>::type& get() const {
    return matcher<Op>::get(*this);
  }
  template<Opcode op>
  typename op_matcher<op>::type& get() {
    return op_matcher<op>::get(*this);
  }
  template<Opcode op>
  const typename op_matcher<op>::type& get() const {
    return op_matcher<op>::get(*this);
  }

  /////////////////////////////////////////////////////////////////////////////
  // Data members.

  Opcode op;

  /*
   * Instruction position, currently used only in vasm-xls.
   */
  unsigned pos;

  /*
   * If present, the IRInstruction this Vinstr was originally created from.
   */
  const IRInstruction* origin{nullptr};

  /*
   * A union of all possible instructions, descriminated by the op field.
   */
#define O(name, imms, uses, defs) jit::name name##_;
  union { VASM_OPCODES };
#undef O
};

extern const char* vinst_names[];

///////////////////////////////////////////////////////////////////////////////

#define O(name, ...)                              \
  template<> struct Vinstr::matcher<name> {       \
    using type = jit::name;                       \
    static type& get(Vinstr& inst) {              \
      assertx(inst.op == name);                   \
      return inst.name##_;                        \
    }                                             \
    static const type& get(const Vinstr& inst) {  \
      assertx(inst.op == name);                   \
      return inst.name##_;                        \
    }                                             \
  };                                              \
  template<> struct Vinstr::op_matcher<Vinstr::name> {  \
    using type = jit::name;                       \
    static type& get(Vinstr& inst) {              \
      assertx(inst.op == name);                   \
      return inst.name##_;                        \
    }                                             \
    static const type& get(const Vinstr& inst) {  \
      assertx(inst.op == name);                   \
      return inst.name##_;                        \
    }                                             \
  };
VASM_OPCODES
#undef O

///////////////////////////////////////////////////////////////////////////////

/*
 * Whether `inst' is a block-terminating instruction.
 */
bool isBlockEnd(const Vinstr& inst);

/*
 * The register width specification of `op'.
 *
 * If `op' is an instruction whose non-flags register arguments are all a
 * certain width, return that width; otherwise, return Width::Any.
 *
 * In particular, Width::Any is returned for intrinsics, architecture-specific
 * instructions, zero-extending or truncating reg moves, branches, pushes/pops,
 * and floating-point conversions.  All other instructions have operands of
 * fixed and uniform width.
 */
Width width(Vinstr::Opcode op);

///////////////////////////////////////////////////////////////////////////////
}}

#endif
