#include "grammar.hpp"
#include "instruction.hpp"
#include "library.hpp"

#include <ffi.h>

std::string source = R"(
str(12.5)
print(13, 14, 'hello world', 12)
if (false)
	int if_1 = 1
else
	int else_1 = 4

float x = 200 * 16 + 10.0/2.5 - 3
int y = 20
string z = "Hello world!"
bool w = false

float t = x * y
)";

machine m;

using nabu::parser::rd::grammar;
using nabu::parser::_lexvalue;

using clause = alias <lparen, expression, rparen>;
using body = alias <lbrace, repeat <statement>, rbrace>;
using conditional_body = option <statement, body>;

using if_branch = alias <k_if, clause, conditional_body>;
using else_branch = alias <k_else, conditional_body>;

using k_else_if = alias <k_else, k_if>;
using else_if_branch = alias <k_else_if, clause, conditional_body>;

register(k_else_if)
register(if_branch)
register(else_branch)
register(else_if_branch)

// TODO: custom allocator for improved performance?

struct _addr_info {
	int ncjmp;
	int end;
};

std::map <_lexvalue *, _addr_info> branch_addresses;

// Clauses are for braching and loops:
// 	evaluate the expression in the clause and
// 	only then possibly jump
define_action(clause)
{
	push(m, {_instruction::Type::eNcjmp, -1});
	branch_addresses[lptr.get()] = {
		(int) m.instructions.size() - 1, -1
	};
}

define_action(if_branch)
{
	push(m, {_instruction::Type::eJmp, -1});

	_lexvalue *clause = get <vec> (lptr)[1].get();
	_addr_info &info = branch_addresses[clause];
	info.end = m.instructions.size();
}

define_action(else_if_branch)
{
	push(m, {_instruction::Type::eJmp, -1});

	_lexvalue *clause = get <vec> (lptr)[1].get();
	_addr_info &info = branch_addresses[clause];
	info.end = m.instructions.size();
}

using branch = alias <if_branch, repeat <else_if_branch>, option <else_branch, void>>;

define_action(branch)
{
	// Resolve jump addresses
	std::cout << "lptr = " << lptr->str() << std::endl;
	vec v = get <vec> (lptr);

	// Should always be 3 elements, even without else-if and else
	assert(v.size() == 3);

	// Get the clauses in the branch address map
	std::vector <_lexvalue *> clauses;

	// If-branch
	_lexvalue *if_clause = get <vec> (v[0])[1].get();
	clauses.push_back(if_clause);

	// Else-if-branches
	vec else_ifs = get <vec> (v[1]);
	for (auto &else_if : else_ifs) {
		_lexvalue *else_if_clause = get <vec> (else_if)[1].get();
		clauses.push_back(else_if_clause);
	}

	// We don't need the else-branch clause,
	// 	since it's always the last one
	std::cout << "Clauses" << std::endl;
	for (auto &clause : clauses)
		std::cout << clause << std::endl;


	std::cout << "Branch addresses" << std::endl;
	for (auto &p : branch_addresses)
		std::cout << p.first << " -> (" << p.second.ncjmp << ", " << p.second.end << ")" << std::endl;

	// Fix negative conditional jump (eNcjmp) addresses
	for (int i = 0; i < clauses.size(); i++) {
		_addr_info &info = branch_addresses[clauses[i]];
		int pc = info.ncjmp;
		int address = info.end;

		_instruction &instruction = m.instructions[pc];
		assert(instruction.type == _instruction::Type::eNcjmp);
		instruction.op1 = address;
	}
	
	// Fix jump (eJmp) addresses after body of each branch
	int end = m.instructions.size();
	for (int i = 0; i < clauses.size(); i++) {
		_addr_info &info = branch_addresses[clauses[i]];
		_instruction &instruction = m.instructions[info.end - 1];
		assert(instruction.type == _instruction::Type::eJmp);
		instruction.op1 = end;
	}
}

using function_call = alias <
	identifier, lparen, repeat <
		option <alias <expression, comma>, expression>
	>, rparen>;

define_action(function_call)
{
	vec v = get <vec> (lptr);
	std::string name = get <std::string> (v[0]);
	assert(v.size() == 4);

	v = get <vec> (v[2]);
	int nargs = v.size();
	std::cout << "Nargs = " << nargs << std::endl;
	std::cout << "Elements:" << std::endl;
	for (auto &e : v)
		std::cout << e->str() << std::endl;
	if (m.functions.map_ext.count(name) > 0) {
		// External function
		int index = m.functions.map_ext[name];
		push(m, {_instruction::Type::eCallExt, index, nargs});
	} else {
		std::cout << "Unknown function: " << name << std::endl;
	}
}

int main()
{
	using namespace nabu;
	
	// Load libraries
	// TODO: refactor to std
	void *libhandle = dlopen("/home/venki/kobra/bin/lib/libio_arbok.so", RTLD_LAZY);
	if (!libhandle) {
		fprintf(stderr, "dlopen error: %s\n", dlerror());
		exit(1);
	}

	printf("dlopen success: handle %p\n", libhandle);

	typedef void (*importer_t)(std::vector <std::pair <std::string, std::string>> &);
	importer_t func = (importer_t) dlsym(libhandle, "import");
	if (!func) {
		fprintf(stderr, "dlsym error: %s\n", dlerror());
		exit(1);
	}

	printf("dlsym success: func %p\n", func);

	std::vector <std::pair <std::string, std::string>> args;
	func(args);

	for (auto pr : args) {
		auto ext = compile_signature(pr.first, pr.second, libhandle);
		m.functions.map_ext.insert({ext.name, m.functions.externals.size()});
		m.functions.externals.push_back(ext);
		std::cout << "Successfully compiled signature: " << pr.first << std::endl;
	}


	parser::Queue q = parser::lexq <identifier> (source);

#if 0

	std::cout << "Queue size: " << q.size() << std::endl;
	while (!q.empty()) {
		parser::lexicon lptr = q.front();
		q.pop_front();

		if (lptr == nullptr) {
			std::cout << "nullptr" << std::endl;
			continue;
		}

		std::cout << "lexicon: " << lptr->name << " = " << lptr->str() << std::endl;
	}

#else

	using g_input = grammar <function_call>;

	parser::rd::DualQueue dq(q);
	g_input::value(dq);

	std::cout << "Top of queue:\n";
	int i = 6;
	while (i--) {
		parser::lexicon lptr = q.front();
		q.pop_front();

		if (lptr == nullptr) {
			std::cout << "nullptr" << std::endl;
			continue;
		}

		std::cout << "\tlexicon: " << lptr->name << " = " << lptr->str() << std::endl;
	}

	// Add an end instruction for padding
	push(m, _instruction::Type::eEnd);

	dump(m);

	exec(m);
	dump(m);

#endif

	return 0;
}
