// tokenize.cpp

#ifdef _MSC_VER
#pragma warning ( disable : 4786 )
#endif//_MSC_VER

#include <string>
#include <vector>
#include <conio.h>

#include "assert.h"
#include "tokenize.h"
#include "skip_ws.h"

using std::string;
using std::vector;

void tokenize ( const string& text, vector<string>& tokens )
{
	tokens.resize ( 0 );
	string s ( text );
	char* p = &s[0];
	while ( *p )
	{
		// skip whitespace
		p = skip_ws ( p );
		// check for literal string
		if ( *p == '\"' )
		{
			// skip initial quote
			char* end = p + 1;
			for ( ;; )
			{
				if ( *end == '\\' )
				{
					end++;
					switch ( *end )
					{
					case 'x':
					case 'X':
						ASSERT(0); // come back to this....
						break;
					case '0':
						ASSERT(0);
						break;
					default:
						p++;
						break;
					}
				}
				else if ( *end == '\"' )
				{
					end++;
					break;
				}
			}
			tokens.push_back ( string ( p, end-p ) );
			p = end;
		}
		else if ( __iscsymf(*p) )
		{
			char* end = p + 1;
			while ( __iscsym ( *end ) )
				end++;
			tokens.push_back ( string ( p, end-p ) );
			p = end;
		}
		else if ( isdigit(*p) || *p == '.' )
		{
			char* end = p;
			while ( isdigit(*end) )
				end++;
			bool f = false;
			if ( *end == '.' )
			{
				end++;
				while ( isdigit(*end) )
					end++;
				f = true;
			}
			if ( *end == 'f' || *end == 'F' )
				end++;
			else if ( !f && ( *end == 'l' || *end == 'L' ) )
				end++;
			tokens.push_back ( string ( p, end-p ) );
			p = end;
		}
		else switch ( *p )
		{
		case '.':
			tokens.push_back ( "." );
			p++;
			break;
		case ',':
			tokens.push_back ( "," );
			p++;
			break;
		case '(':
			tokens.push_back ( "(" );
			p++;
			break;
		case ')':
			tokens.push_back ( ")" );
			p++;
			break;
		case '{':
			tokens.push_back ( "{" );
			p++;
			break;
		case '}':
			tokens.push_back ( "}" );
			p++;
			break;
		case '[':
			tokens.push_back ( "[" );
			p++;
			break;
		case ']':
			tokens.push_back ( "]" );
			p++;
			break;
		case ';':
			tokens.push_back ( ";" );
			p++;
			break;
		case '=':
			switch ( p[1] )
			{
			case '=':
				tokens.push_back ( string ( p, 2 ) );
				p += 2;
				break;
			default:
				tokens.push_back ( "=" );
				p++;
				break;
			}
			break;
		case ':':
			switch ( p[1] )
			{
			case ':':
				tokens.push_back ( string ( p, 2 ) );
				p += 2;
				break;
			default:
				tokens.push_back ( ":" );
				p++;
				break;
			}
			break;
		case '*':
			switch ( p[1] )
			{
			case '=':
				tokens.push_back ( string ( p, 2 ) );
				p += 2;
				break;
			default:
				tokens.push_back ( "*" );
				p++;
				break;
			}
			break;
		case '/':
			switch ( p[1] )
			{
			case '=':
				tokens.push_back ( string ( p, 2 ) );
				p += 2;
				break;
			default:
				tokens.push_back ( "/" );
				p++;
				break;
			}
			break;
		case '+':
			switch ( p[1] )
			{
			case '+':
			case '=':
				tokens.push_back ( string ( p, 2 ) );
				p += 2;
				break;
			default:
				tokens.push_back ( "+" );
				p++;
				break;
			}
			break;
		case '-':
			switch ( p[1] )
			{
			case '-':
			case '=':
				tokens.push_back ( string ( p, 2 ) );
				p += 2;
				break;
			default:
				tokens.push_back ( "-" );
				p++;
				break;
			}
			break;
		case '#':
			while ( *p != '\n' )
				p++;
			break;
		default:
			printf ( "choked on '%c' in tokenize() - press any key to continue\n", *p );
			getch();
			p++;
			break;
		}
	}
}