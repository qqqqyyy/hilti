
#include "../hilti.h"

#include "storer.h"
#include "codegen.h"

using namespace hilti;
using namespace codegen;

Storer::Storer(CodeGen* cg) : CGVisitor<int, llvm::Value*>(cg, "codegen::Storer")
{
}

Storer::~Storer()
{
}

void Storer::llvmStore(shared_ptr<Expression> target, llvm::Value* value)
{
    setArg1(value);
    call(target);
}

void Storer::visit(expression::Variable* v)
{
    call(v->variable());
}

void Storer::visit(expression::Parameter* p)
{
    // I don't think this actually works. We may need to create temporary
    // variables holding the parameters so that we can potentially change
    // them.
    auto val = arg1();
    auto addr = cg()->llvmParameter(p->parameter());
    builder()->CreateStore(val, addr);
}

void Storer::visit(expression::CodeGen* c)
{
    auto val = arg1();
    llvm::Value* addr = reinterpret_cast<llvm::Value*>(c->cookie());
    builder()->CreateStore(val, addr);
}

void Storer::visit(variable::Global* v)
{
    auto val = arg1();
    auto addr = cg()->llvmGlobal(v);
    builder()->CreateStore(val, addr, true); // Mark as volatile.
}

void Storer::visit(variable::Local* v)
{
    auto val = arg1();
    auto name = v->id()->name();
    auto addr = cg()->llvmLocal(name);
    cg()->builder()->CreateStore(val, addr);
}
