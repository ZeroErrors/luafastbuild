#include "LuaNodeGraph.h"
#include "LuaFunctions.h"
#include "Utils/FBuildAccessBypass.h"

#include "Tools/FBuild/FBuildCore/FLog.h"
#include "Tools/FBuild/FBuildCore/FBuild.h"
#include "Tools/FBuild/FBuildCore/BFF/BFFParser.h"
#include "Tools/FBuild/FBuildCore/Graph/NodeGraph.h"
#include "Tools/FBuild/FBuildCore/Graph/SettingsNode.h"

#include "Core/Env/ErrorFormat.h"
#include "Core/FileIO/FileIO.h"
#include "Core/Mem/SmallBlockAllocator.h"
#include "Core/Profile/Profile.h"
#include "Core/Tracing/Tracing.h"

#include "lua.h"
#include "lualib.h"
#include "luacode.h"

// Note: Needed to free Lua allocations
#include <malloc.h>

LuaNodeGraph::LuaNodeGraph( lua_State * L ) : L(L)
{
}

// Note: @Modified version of NodeGraph::ParseFromRoot
bool LuaNodeGraph::ParseFromRoot( const AString & filename )
{
    // @Modified: Bypass access
    auto _m_UsedFiles = __NodeGraph::m_UsedFiles::ptr(this);
    ASSERT( _m_UsedFiles->IsEmpty() ); // NodeGraph cannot be recycled

    BFFParser bffParser( *this );

    // @Modfied: Access bypass
    __BFFParser::CreateBuiltInVariables::call(&bffParser);

    LuaUserdata userdata = { this, &bffParser };
    lua_callbacks(L)->userdata = &userdata;

    bool ok = false;
    if ( filename.EndsWithI(".bff") )
    {

        ok = ParseBffFile( bffParser, filename );
    }
    else
    {
        ok = ParseLuaFile( filename.Get() );
    }

    lua_callbacks(L)->userdata = nullptr;

    if ( ok )
    {
        // Store a pointer to the SettingsNode as defined by the BFF, or create a
        // default instance if needed.
        const AStackString<> settingsNodeName( "$$Settings$$" );
        const Node * settingsNode = FindNode( settingsNodeName );

        // @Modified: Bypass access
        auto _m_Settings = __NodeGraph::m_Settings::ptr(this);
        *_m_Settings = settingsNode ? settingsNode->CastTo< SettingsNode >() : CreateSettingsNode( settingsNodeName ); // Create a default

        // Parser will populate m_UsedFiles
        // @Modified: Bypass access
        const Array<BFFFile *> & usedFiles = __BFFParser::GetUsedFiles::call(&bffParser);
        _m_UsedFiles->SetCapacity( _m_UsedFiles->GetSize() + usedFiles.GetSize() );
        for ( const BFFFile * f : usedFiles )
        {
            _m_UsedFiles->EmplaceBack( f->GetFileName(), f->GetTimeStamp(), f->GetHash() );
        }
    }

    return ok;
}

bool LuaNodeGraph::ParseBffFile( BFFParser & bffParser, const AString & filename )
{
    auto _m_Tokenizer = __BFFParser::m_Tokenizer::ptr(&bffParser);
    if ( !_m_Tokenizer->TokenizeFromFile( filename ) )
    {
        FLOG_ERROR("Error reading BFF '%s'", filename.Get());
        return false;
    }

    const Array<BFFToken>& tokens = _m_Tokenizer->GetTokens();
    if ( tokens.IsEmpty() )
    {
        return true; // An empty file is ok
    }

    BFFTokenRange range( tokens.Begin(), tokens.End() );
    if ( !bffParser.Parse( range ) )
    {
        FLOG_ERROR("Error reading BFF '%s'", filename.Get());
    }

    return true;
}

// Note: @Modified version of NodeGraph::ParseFromRoot
bool LuaNodeGraph::ParseLuaFile( const char * filename )
{
    // @Modified: Bypass access
    auto _m_UsedFiles = __NodeGraph::m_UsedFiles::ptr(this);

    BFFFile file;
    if ( !file.Load(AString( filename ), nullptr ) )
    {
        FLOG_ERROR( "Error reading Lua '%s'", filename );
        return false;
    }

    _m_UsedFiles->EmplaceBack( file.GetFileName(), file.GetTimeStamp(), file.GetHash() );

    AString chunkname("=");
    chunkname += filename;

    const AString& content = file.GetSourceFileContents();

    size_t bytecodeSize = 0;
    char* bytecode = luau_compile(content.Get(), content.GetLength(), NULL, &bytecodeSize);
    int result = luau_load(L, chunkname.Get(), bytecode, bytecodeSize, 0);
    free(bytecode); // Note: Since this was allocated by Lua we need to use regular free function

    int status = 0;
    if (result == 0)
    {
        status = lua_resume(L, NULL, 0);
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

        error += "\nstacktrace:\n";
        error += lua_debugtrace(L);

        FLOG_ERROR("%s", error.Get());
    }

    return status == 0;
}

// Note: Modified version of NodeGraph::Initialize
NodeGraph * LuaNodeGraph::Initialize( lua_State *L,
                                      const AString & file,
                                      const char * nodeGraphDBFile,
                                      bool forceMigration )
{
    using LoadResult = NodeGraph::LoadResult;
    PROFILE_FUNCTION;

    ASSERT( !file.IsEmpty() ); // must be supplied (or left as default)
    ASSERT( nodeGraphDBFile ); // must be supplied (or left as default)

    // Try to load the old DB
    NodeGraph * oldNG = FNEW( NodeGraph );
    LoadResult res = oldNG->Load( nodeGraphDBFile );

    // Tests can force us to do a migration even if the DB didn't change
    if ( forceMigration )
    {
        // If migration can't be forced, then the test won't function as expected
        // so we want to catch that failure.
        ASSERT( ( res == LoadResult::OK ) || ( res == LoadResult::OK_BFF_NEEDS_REPARSING ) );

        res = LoadResult::OK_BFF_NEEDS_REPARSING; // forces migration
    }

    // What happened?
    switch ( res )
    {
        case LoadResult::MISSING_OR_INCOMPATIBLE:
        case LoadResult::LOAD_ERROR:
        case LoadResult::LOAD_ERROR_MOVED:
        {
            // Failed due to moved DB?
            if ( res == LoadResult::LOAD_ERROR_MOVED )
            {
                // Is moving considerd fatal?
                if ( FBuild::Get().GetOptions().m_ContinueAfterDBMove == false )
                {
                    // Corrupt DB or other fatal problem
                    FDELETE( oldNG );
                    return nullptr;
                }
            }

            // Failed due to corrupt DB? Make a backup to assist triage
            if ( res == LoadResult::LOAD_ERROR )
            {
                AStackString<> corruptDBName( nodeGraphDBFile );
                corruptDBName += ".corrupt";
                FileIO::FileMove( AStackString<>( nodeGraphDBFile ), corruptDBName ); // Will overwrite if needed
            }

            // Create a fresh DB by parsing the BFF
            FDELETE( oldNG );
            LuaNodeGraph * newNG = FNEW( LuaNodeGraph )(L);

            // Note: @Modified: if ( newNG->ParseFromRoot( bffFile ) == false )
            if ( newNG->ParseFromRoot( file ) == false )
            {
                FDELETE( newNG );
                return nullptr; // ParseFromRoot will have emitted an error
            }
            return newNG;
        }
        case LoadResult::OK_BFF_NEEDS_REPARSING:
        {
            // Create a fresh DB by parsing the modified BFF
            LuaNodeGraph * newNG = FNEW( LuaNodeGraph )(L);
            // Note: @Modified: if ( newNG->ParseFromRoot( bffFile ) == false )
            if ( newNG->ParseFromRoot( file ) == false )
            {
                FDELETE( newNG );
                FDELETE( oldNG );
                return nullptr;
            }

            // Migrate old DB info to new DB
            // @Modified: Access bypass
            __NodeGraph::Migrate::call( newNG, *oldNG );
            FDELETE( oldNG );

            return newNG;
        }
        case LoadResult::OK:
        {
            // Nothing more to do
            return oldNG;
        }
    }

    ASSERT( false ); // Should not get here
    return nullptr;
}
