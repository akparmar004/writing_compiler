#include "defs.h"
#include "data.h"
#include "decl.h"

//parsing of declarations

//pass the current token and return a primitive type enum value and also scan in the next token
int parse_type(void) 
{
  	int type;
  	switch(Token.token) 
	{
    		case T_VOID:
      			type = P_VOID;
      			break;
    		case T_CHAR:
      			type = P_CHAR;
      			break;
    		case T_INT:
      			type = P_INT;
      			break;
    		case T_LONG:
      			type = P_LONG;
      			break;
    		default:
      			fatald("Bad type, token", Token.token);
  	}

  	//scan in one or more further '*' tokens and determine the correct pointer type
  	while (1) 
  	{
    		scan(&Token);
    		if(Token.token != T_STAR)
      			break;
    		
		type = pointer_to(type);
  	}

  	//we will return with next token in "Token"..
  	return type;

}

// variable_declaration: type identifier ';' | type identifier '[' INTLIT ']' ';' ;

//parse the declaration of var or array with a given size...
//the identifier has been scanned & we have the type
symt *var_declaration(int type, int class) 
{
	symt *sym = NULL;

  	//see if this has already been declared
  	switch(class) 
	{
    		case C_GLOBAL:
      			if(findglob(Text) != NULL)
				fatals("Duplicate global variable declaration", Text);
    		case C_LOCAL:
    		case C_PARAM:
      			if(findlocl(Text) != NULL)
				fatals("Duplicate local variable declaration", Text);
  	}

  	//text now has the identifier's name, if the next token is a '['
  	if(Token.token == T_LBRACKET) 
	{
    		//skip past the '['
    		scan(&Token);

    		// Check we have an array size
    		if(Token.token == T_INTLIT) 
		{
      			// Add this as a known array and generate its space in assembly.
      			// We treat the array as a pointer to its elements' type
      			switch(class) 
			{
				case C_GLOBAL:
	  				sym = addglob(Text, pointer_to(type), S_ARRAY, class, Token.intvalue);
	  				break;
				case C_LOCAL:
				case C_PARAM:
	  				fatal("For now, declaration of local arrays is not implemented");
      			}
    		}
    	//ensure we have a following ']'
    	scan(&Token);
    	match(T_RBRACKET, "]");
  	} 
	else 
	{
    		//add this as a known scalar and generate its space in assembly
    		switch(class) 
		{
      			case C_GLOBAL:
				sym = addglob(Text, type, S_VARIABLE, class, 1);
				break;
      			case C_LOCAL:
				sym = addlocl(Text, type, S_VARIABLE, class, 1);
				break;
      			case C_PARAM:
				sym = addparm(Text, type, S_VARIABLE, class, 1);
				break;
    		}
  	}
  	return sym;
}

// param_declaration: <null> | variable_declaration | variable_declaration ',' param_declaration

//parse the parameters in parentheses after the function name.
//Add them as symbols to the symbol table and return the number
//of parameters. If funcsym is not NULL, there is an existing function
//prototype, and the function has this symbol table pointer.
static int param_declaration(symt *funcsym) 
{
  	int type;
  	int paramcnt = 0;
  	symt *protoptr = NULL;

  	//if there is a prototype, get the pointer to the first prototype parameter
  	if(funcsym != NULL)
    		protoptr = funcsym -> member;

  	//loop until the final right parentheses
  	while(Token.token != T_RPAREN) 
	{
    		//get the type and identifier and add it to the symbol table
    		type = parse_type();
    		ident();

    		//we have an existing prototype, check that this type matches the prototype.
    		if(protoptr != NULL) 
		{
      			if(type != protoptr->type)
				fatald("Type doesn't match prototype for parameter", paramcnt + 1);
      			protoptr = protoptr->next;
    		} 
		else 
		{
      			//add new param to the new parameter list
      			var_declaration(type, C_PARAM);
    		}
    		paramcnt++;

    		//must have a ',' or ')' at this point
    		switch(Token.token) 
		{
      			case T_COMMA:
				scan(&Token);
				break;
      			case T_RPAREN:
				break;
      			default:
				fatald("Unexpected token in parameter list", Token.token);
    		}
  	}

  	//check that the number of parameters in this list matches any existing prototype
  	if((funcsym != NULL) && (paramcnt != funcsym->nelems))
    		fatals("Parameter count mismatch for function", funcsym->name);

  	//return the count of parameters
  	return paramcnt;
}


//function_declaration: type identifier '(' ')' compound_statement 

//parse the declaration of a simplistic function, and The identifier has been scanned & we have the type
ast *function_declaration(int type) 
{
	ast *tree, *finalstmt;
  	symt *oldfuncsym, *newfuncsym = NULL;
  	int endlabel, paramcnt;

  	//text has the identifier's name, if this exists and is a
	//function, get the id, otherwise set oldfuncsym to NULL.
  	if((oldfuncsym = findsymbol(Text)) != NULL)
    		if(oldfuncsym -> stype != S_FUNCTION)
      			oldfuncsym = NULL;

  	//if this is a new function declaration, get a label-id for the end label, and add the function
  	//to the symbol table,
  	if(oldfuncsym == NULL) 
	{
    		endlabel = genlabel();
    		newfuncsym = addglob(Text, type, S_FUNCTION, C_GLOBAL, endlabel);
  	}
  	
	//scan in the '(', any parameters and the ')', pass in any existing function prototype pointer
  	lparen();
  	paramcnt = param_declaration(oldfuncsym);
  	rparen();

  	//if this is new function declaration, then update the function symbol entry with number of parameters
  	//and also copy the parameter list into the function's node.
  	if(newfuncsym) 
	{
    		newfuncsym -> nelems = paramcnt;
    		newfuncsym -> member = Parmhead;
    		oldfuncsym = newfuncsym;
  	}
  	
	//clear out the parameter list
  	Parmhead = Parmtail = NULL;

  	//declaration ends in a semicolon, only a prototype.
  	if(Token.token == T_SEMI) 
	{
    		scan(&Token);
    		return NULL;
  	}

  	//this is not just a prototype.
  	//set the Functionid global to the function's symbol pointer
  	Functionid = oldfuncsym;

  	//get the AST tree for the compound statement
  	tree = compound_statement();

  	//if the function type isn't P_VOID ..
  	if(type != P_VOID) 
	{

    		//error if no statements in the function
    		if(tree == NULL)
      			fatal("No statements in function with non-void type");

    		//check that the last AST operation in compound statement was a return statement
    		finalstmt = (tree->op == A_GLUE) ? tree->right : tree;
    	
		if(finalstmt == NULL || finalstmt->op != A_RETURN)
      			fatal("No return for function with non-void type");
  	}
  	//return an A_FUNCTION node which has the function's symbol pointer and the compound statement sub-tree
  	return mkastunary(A_FUNCTION, type, tree, oldfuncsym, endlabel);
}

//parse one or more global declars, either vars or functs
void global_declarations(vorid) 
{
  	ast *tree;
  	int type;

  	while(1) 
	{
    		//we have to read past the type and identifier to see either a '(' for a function declaration
    		//or a ',' or ';' for a variable declaration. text is filled in by the ident() call.
    		type = parse_type();
    		ident();

    		if(Token.token == T_LPAREN) 
		{
			//parse function declaration and generate assembly code..
       			tree = function_declaration(type);
       			
			if(tree == NULL)
				continue;
		
			if(O_dumpAST) 
			{
				dumpAST(tree, NOLABEL, 0); 
				fprintf(stdout, "\n\n"); 
			}
       			genAST(tree, NOLABEL, 0);

			//free the symbols
			freeloclsyms();
    		} 
		else 
		{
       			//parse the global variable declaration
       			var_declaration(type, C_GLOBAL);
			semi();
    		}

    		//stop when we have reached EOF
    		if(Token.token == T_EOF)
      			break;
  	}
}	
