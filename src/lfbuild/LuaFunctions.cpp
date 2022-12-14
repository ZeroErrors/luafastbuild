#include "LuaFunctions.h"
#include "LuaNodeGraph.h"
#include "Functions/FunctionExecuteLua.h"
#include "Utils/FBuildAccessBypass.h"

#include "Tools/FBuild/FBuildCore/FLog.h"
#include "Tools/FBuild/FBuildCore/BFF/BFFFile.h"
#include "Tools/FBuild/FBuildCore/BFF/BFFParser.h"
#include "Tools/FBuild/FBuildCore/BFF/Tokenizer/BFFToken.h"
#include "Tools/FBuild/FBuildCore/BFF/Tokenizer/BFFTokenRange.h"
#include "Tools/FBuild/FBuildCore/BFF/Functions/Function.h"

#include "Core/FileIO/PathUtils.h"

#include "lua.h"
#include "lualib.h"
#include "luacode.h"

// Note: Needed for memset
#include <memory.h>
// Note: Needed to free Lua allocations
#include <malloc.h>
// Note: Needed for floor function
#include <math.h>

// From Function.cpp
extern Array<const Function *> g_Functions;

void lua_to_bfftokens( lua_State * L, int idx, const BFFFile & file, Array<BFFToken> & tokens );

// Copied from BFFTokenizer::ExpandIncludePath
void ExpandIncludePath( const AString & currentFile, AString & includePath )
{
    // Includes are relative to current file, unless full paths
    if ( PathUtils::IsFullPath( includePath ) == false )
    {
        // Determine path from current file
        const char * lastSlash = currentFile.FindLast( NATIVE_SLASH );
        lastSlash = lastSlash ? lastSlash : currentFile.FindLast( OTHER_SLASH );
        lastSlash = lastSlash ? ( lastSlash + 1 ): currentFile.Get(); // file only, truncate to empty
        AStackString<> tmp( currentFile.Get(), lastSlash );
        tmp += includePath;
        includePath = tmp;
    }
}

/* Find the size of the array on the top of the Lua stack
 * -1   object (not a pure array)
 * >=0  elements in array
 */
static int lua_array_length( lua_State *l, int idx )
{
    double k;
    int max;
    int items;

    max = 0;
    items = 0;

    lua_pushnil(l);

    // Since we pushed onto the stack we need to adjust the index
    if (idx < 0) idx--;

    /* startkey */
    while (lua_next(l, idx) != 0)
    {
        /* key, value */
        if (lua_type(l, -2) == LUA_TNUMBER)
        {
            k = lua_tonumber(l, -2);
            if (k)
            {
                /* Integer >= 1 ? */
                if (floor(k) == k && k >= 1)
                {
                    if (k > max)
                        max = (int)k;
                    items++;
                    lua_pop(l, 1);
                    continue;
                }
            }
        }

        /* Must not be an array (non integer key) */
        lua_pop(l, 2);
        return -1;
    }

    #define DEFAULT_SPARSE_RATIO 2
    #define DEFAULT_SPARSE_SAFE 10
    /* Encode excessively sparse arrays as objects (if enabled) */
    if (DEFAULT_SPARSE_RATIO > 0 &&
        max > items * DEFAULT_SPARSE_RATIO &&
        max > DEFAULT_SPARSE_SAFE) {
        return -1;
    }

    return max;
}

void lua_string_table_content_to_bfftokens( lua_State * L, int idx, const BFFFile & file, Array<BFFToken> & tokens )
{
    lua_pushnil(L);

    // Since we pushed onto the stack we need to adjust the index
    if (idx < 0) idx--;

    /* startkey */
    while (lua_next(L, idx))
    {
        /* key, value */
        size_t keyLen;
        const char* key = lua_tolstring(L, -2, &keyLen);

        AString variable( "." );
        variable.Append(AString( key, key + keyLen ));

        tokens.EmplaceBack(
            file, file.GetSourceFileContents().Get(),
            BFFTokenType::Variable,
            variable
        );

        tokens.EmplaceBack(
            file, file.GetSourceFileContents().Get(),
            BFFTokenType::Operator,
            AStackString( "=" )
        );

        lua_to_bfftokens( L, -1, file, tokens );

        lua_pop(L, 1);
    }
}

void lua_to_bfftokens( lua_State * L, int idx, const BFFFile & file, Array<BFFToken> & tokens )
{
    int type = lua_type(L, idx);
    switch (type)
    {
    case LUA_TNIL:
        tokens.EmplaceBack(
            file, file.GetSourceFileContents().Get(),
            BFFTokenType::Invalid,
            AString::GetEmpty()
        );
        break;
    case LUA_TBOOLEAN:
        tokens.EmplaceBack(
            file, file.GetSourceFileContents().Get(),
            BFFTokenType::Boolean,
            lua_toboolean(L, idx) == 1
        );
        break;
    case LUA_TLIGHTUSERDATA:
        ASSERT(false); // TODO: Support light userdata
        break;
    case LUA_TVECTOR:
        ASSERT(false); // TODO: Support vector
        break;
    case LUA_TTABLE:
    {
        int array_length = lua_array_length( L, idx );
        if (array_length == -1)
        {
            tokens.EmplaceBack(
                file, file.GetSourceFileContents().Get(),
                BFFTokenType::CurlyBracket,
                AStackString( "{" )
            );

            lua_string_table_content_to_bfftokens( L, idx, file, tokens );

            tokens.EmplaceBack(
                file, file.GetSourceFileContents().Get(),
                BFFTokenType::CurlyBracket,
                AStackString( "}" )
            );
        }
        else
        {
            // We know how many tokens we will append so grow the array beforehand.
            tokens.SetCapacity(2 + tokens.GetSize() + array_length * 2);

            tokens.EmplaceBack(
                file, file.GetSourceFileContents().Get(),
                BFFTokenType::CurlyBracket,
                AStackString( "{" )
            );

            int last_index = 1;

            lua_pushnil(L);

            // Since we pushed onto the stack we need to adjust the index
            if (idx < 0) idx--;

            /* startkey */
            while (lua_next(L, idx))
            {
                /* key, value */
                const int index = (int)lua_tonumber(L, -2);

                while (last_index < index)
                {
                    tokens.EmplaceBack(
                        file, file.GetSourceFileContents().Get(),
                        BFFTokenType::Invalid,
                        AString::GetEmpty()
                    );

                    tokens.EmplaceBack(
                        file, file.GetSourceFileContents().Get(),
                        BFFTokenType::Comma,
                        AStackString( "," )
                    );

                    last_index++;
                }

                lua_to_bfftokens( L, -1, file, tokens );
                last_index++;

                if (index < array_length)
                {
                    tokens.EmplaceBack(
                        file, file.GetSourceFileContents().Get(),
                        BFFTokenType::Comma,
                        AStackString( "," )
                    );
                }

                lua_pop(L, 1);
            }

            tokens.EmplaceBack(
                file, file.GetSourceFileContents().Get(),
                BFFTokenType::CurlyBracket,
                AStackString( "}" )
            );
        }
    }
        break;
    case LUA_TNUMBER:
    {
        AStackString str;
        str.AppendFormat("%d", lua_tonumber(L, idx));

        tokens.EmplaceBack(
            file, file.GetSourceFileContents().Get(),
            BFFTokenType::Number,
            str
        );
    }
        break;
    case LUA_TSTRING:
    {
        size_t valueLen;
        const char* value = lua_tolstring(L, idx, &valueLen);

        tokens.EmplaceBack(
            file, file.GetSourceFileContents().Get(),
            BFFTokenType::String,
            value, value + valueLen
        );
    }
        break;
    }
}

static int binding_function( lua_State * L )
{
    lua_Debug ar;
    if (lua_getinfo(L, 0, "n", &ar) == 0)
    {
        luaL_error(L, "Failed to get FASTBuild function name");
    }

    const AString name(ar.name);

    const BFFFile file = BFFFile();
    const BFFToken functionNameToken = BFFToken( file, file.GetSourceFileContents().Get(), BFFTokenType::Function, name);
    const Function * func = Function::Find(name);

    // Note: Copied from BFFParser::ParseFunction
    if ( func->IsUnique() && func->GetSeen() )
    {
        Error::Error_1020_FunctionCanOnlyBeInvokedOnce( &functionNameToken, func );
        luaL_error(L, "Function can only be invoked once");
    }
    func->SetSeen();

    int numArgs = lua_gettop(L); // number of arguments
    int headerArgs = numArgs;

    if (func->NeedsBody())
    {
        if (numArgs == 0)
        {
            Error::Error_1024_FunctionRequiresABody( &functionNameToken, func );
            luaL_error(L, "Function requires a body, should be provided as a table as the last argument");
        }

        // Don't parse the last arg as part of the header
        headerArgs--;
    }

    if (func->NeedsHeader() && headerArgs == 0)
    {
        Error::Error_1023_FunctionRequiresAHeader( &functionNameToken, func );
        luaL_error(L, "Function requires a header");
    }

    Array<BFFToken> headerTokens;
    if (headerArgs > 0)
    {
        for (int i = 1; i <= headerArgs; i++)
        {
            lua_to_bfftokens( L, i, file, headerTokens );

            if (i < headerArgs)
            {
                headerTokens.Append(BFFToken(
                    file, file.GetSourceFileContents().Get(),
                    BFFTokenType::Comma,
                    AString( "," )
                ));
            }
        }
    }

    Array<BFFToken> bodyTokens;
    if (func->NeedsBody())
    {
        luaL_checktype(L, numArgs, LUA_TTABLE);

        lua_string_table_content_to_bfftokens( L, numArgs, file, bodyTokens );
    }

    LuaUserdata * userdata = static_cast<LuaUserdata *>(lua_callbacks(L)->userdata);

    bool result = func->ParseFunction( *userdata->luaNodeGraph,
                                *userdata->bffParser,
                                &functionNameToken,
                                BFFTokenRange( headerTokens.Begin(), headerTokens.End() ),
                                BFFTokenRange( bodyTokens.Begin(), bodyTokens.End() ) );
    if (!result)
    {
        luaL_error(L, "Error executing function: %s", ar.name);
    }

    return 0;
}

static int finishrequire(lua_State* L)
{
    if (lua_isstring(L, -1))
        lua_error(L);

    return 1;
}

static int lua_require(lua_State* L)
{
    const char * name = luaL_checkstring(L, 1);

    luaL_findtable(L, LUA_REGISTRYINDEX, "_MODULES", 1);

    // return the module from the cache
    lua_getfield(L, -1, name);
    if (!lua_isnil(L, -1))
    {
        // L stack: _MODULES result
        return finishrequire(L);
    }

    lua_pop(L, 1);

    lua_Debug ar;
    if (lua_getinfo(L, lua_stackdepth(L) - 1, "s", &ar) == 0)
    {
        luaL_error(L, "Failed to get lua source");
    }

    AString currentFile;
    switch (*ar.source)
    {
    case '=':
        currentFile = AString(ar.source + 1);
        break;
    case '@':
    default:
        luaL_error(L, "unknown lua source format");
        break;
    }

    // TODO: Support returning a bff file as a module that can be executed later and a table can be passed for the current scope?
    AString filename(name);
    if (!filename.EndsWithI(".lua"))
    {
        filename += ".lua";
    }

    ExpandIncludePath( currentFile, filename );

    BFFFile file;
    if ( !file.Load( filename, nullptr ) ) // TODO: Improve error messages (they currenly say BFF when we are loading a lua file here)
    {
        luaL_error(L, "error reading lua '%s'", name);
    }

    LuaUserdata * userdata = static_cast<LuaUserdata *>(lua_callbacks(L)->userdata);

    // module needs to run in a new thread, isolated from the rest
    // note: we create ML on main thread so that it doesn't inherit environment of L
    lua_State* GL = lua_mainthread(L);
    lua_State* ML = lua_newthread(GL);
    lua_xmove(GL, L, 1);

    // new thread needs to have the globals sandboxed
    luaL_sandboxthread(ML);

    AString chunkname( "=" );
    chunkname += filename;

    // now we can compile & run module on the new thread
    const AString& content = file.GetSourceFileContents();

    size_t bytecodeSize = 0;
    char* bytecode = luau_compile(content.Get(), content.GetLength(), NULL, &bytecodeSize);
    int result = luau_load(ML, chunkname.Get(), bytecode, bytecodeSize, 0);
    free(bytecode); // Note: Since this was allocated by Lua we need to use regular free function

    int status = 0;
    if (result == 0)
    {
        status = lua_resume(ML, L, 0);

        if (status == 0)
        {
            if (lua_gettop(ML) == 0)
                lua_pushstring(ML, "module must return a value");
            else if (!lua_istable(ML, -1) && !lua_isfunction(ML, -1))
                lua_pushstring(ML, "module must return a table or function");
        }
        else if (status == LUA_YIELD)
        {
            lua_pushstring(ML, "module can not yield");
        }
        else if (!lua_isstring(ML, -1))
        {
            lua_pushstring(ML, "unknown error while running module");
        }
    }
    else
    {
        status = LUA_ERRSYNTAX;
    }

    if (status != 0)
    {
        AString error;
        if (status == LUA_YIELD)
        {
            error = "thread yielded unexpectedly";
        }
        else if (const char* str = lua_tostring(L, -1))
        {
            error = str;
        }

        luaL_error(L, "error loading %s: %s", name, error.Get());
    }

    // there's now a return value on top of ML; L stack: _MODULES ML
    lua_xmove(ML, L, 1);
    lua_pushvalue(L, -1);
    lua_setfield(L, -4, name);

    if (status == 0)
    {
        // @Modified: Bypass access
        auto _m_UsedFiles = __NodeGraph::m_UsedFiles::ptr(userdata->luaNodeGraph);
        _m_UsedFiles->EmplaceBack( file.GetFileName(), file.GetTimeStamp(), file.GetHash() );
    }

    // L stack: _MODULES ML result
    return finishrequire(L);
}

static int lua_execute_bff(lua_State* L)
{
    const char * name = luaL_checkstring(L, 1);

    lua_Debug ar;
    if (lua_getinfo(L, lua_stackdepth(L) - 1, "s", &ar) == 0)
    {
        luaL_error(L, "Failed to get lua source");
    }

    AString currentFile;
    switch (*ar.source)
    {
    case '=':
        currentFile = AString(ar.source + 1);
        break;
    case '@':
    default:
        luaL_error(L, "unknown lua source format");
        break;
    }

    AString filename(name);
    if (!filename.EndsWithI(".bff"))
    {
        filename += ".bff";
    }

    ExpandIncludePath( currentFile, filename );

    LuaUserdata * userdata = static_cast<LuaUserdata *>(lua_callbacks(L)->userdata);

    auto _m_Tokenizer = __BFFParser::m_Tokenizer::ptr(userdata->bffParser);
    if ( !_m_Tokenizer->TokenizeFromFile( filename ) )
    {
        luaL_error(L, "error reading BFF '%s'", filename.Get());
    }

    const Array<BFFToken>& tokens = _m_Tokenizer->GetTokens();
    if ( tokens.IsEmpty() )
    {
        return 0; // An empty file is ok
    }

    BFFTokenRange range( tokens.Begin(), tokens.End() );
    if ( !userdata->bffParser->Parse( range ) )
    {
        luaL_error(L, "error reading lua '%s'", filename.Get());
    }

    return 0;
}

void RegisterLuaFunctions(lua_State * L)
{
    luaL_openlibs(L);

    size_t size = g_Functions.GetSize();
    luaL_Reg * bindings = FNEW_ARRAY( luaL_Reg [ size + 1 ] );
    memset( bindings, 0, sizeof( const luaL_Reg ) * ( size + 1 ) );

    size_t x = 0;
    size_t i = 0;
    for (; i < size; i++)
    {
        const Function * f = g_Functions[i];

        // Exclude functions that already exist in Lua or we use Lua langauge features instead.
        //if (f->GetName() == "Error") continue;
        if (f->GetName() == "ForEach") continue;
        if (f->GetName() == "If") continue;
        //if (f->GetName() == "Print") continue;
        if (f->GetName() == "Using") continue;

        bindings[x++] = { f->GetName().Get(), binding_function };
    }

    lua_pushvalue(L, LUA_GLOBALSINDEX);
    luaL_register(L, NULL, bindings);
    lua_pop(L, 1);

    FDELETE_ARRAY bindings;

    static const luaL_Reg globalBindings[] = {
        {"require", lua_require},
        {"execute_bff", lua_execute_bff},
        {NULL, NULL}
    };

    lua_pushvalue(L, LUA_GLOBALSINDEX);
    luaL_register(L, NULL, globalBindings);
    lua_pop(L, 1);

    luaL_sandbox(L);
    luaL_sandboxthread(L);

    // We must replace g_Functions with a resizable array.
    Array<const Function *> temp;
    temp.Append(g_Functions);
    g_Functions = Move( temp );

    // Allow BFF to call into Lua code.
    g_Functions.Append( FNEW( FunctionExecuteLua ) );
}
