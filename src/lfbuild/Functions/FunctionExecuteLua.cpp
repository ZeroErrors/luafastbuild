#include "FunctionExecuteLua.h"

#include "LuaNodeGraph.h"
#include "Utils/FBuildAccessBypass.h"

#include "Tools/FBuild/FBuildCore/FBuild.h"
#include "Tools/FBuild/FBuildCore/FLog.h"
#include "Tools/FBuild/FBuildCore/BFF/BFFKeywords.h"
#include "Tools/FBuild/FBuildCore/BFF/BFFParser.h"
#include "Tools/FBuild/FBuildCore/BFF/BFFStackFrame.h"
#include "Tools/FBuild/FBuildCore/BFF/Tokenizer/BFFTokenRange.h"

FunctionExecuteLua::FunctionExecuteLua()
: Function( "ExecuteLua" )
{
}

/*virtual*/ bool FunctionExecuteLua::AcceptsHeader() const
{
    return true;
}

/*virtual*/ bool FunctionExecuteLua::NeedsHeader() const
{
    return true;
}

/*virtual*/ bool FunctionExecuteLua::NeedsBody() const
{
    return false;
}

/*virtual*/ bool FunctionExecuteLua::ParseFunction( NodeGraph & nodeGraph,
                                               BFFParser & parser,
                                               const BFFToken * /*functionNameStart*/,
                                               const BFFTokenRange & headerRange,
                                               const BFFTokenRange & /*bodyRange*/ ) const
{
    ASSERT( headerRange.IsEmpty() == false );

    // Grab token
    BFFTokenRange headerIter = headerRange;
    const BFFToken * varToken = headerIter.GetCurrent();
    headerIter++;

    if ( varToken->IsString() )
    {
        // perform variable substitutions
        AStackString< 1024 > tmp;
        if ( BFFParser::PerformVariableSubstitutions( varToken, tmp ) == false )
        {
            return false; // substitution will have emitted an error
        }

        auto _m_CurrentBFFFile = *__BFFParser::m_CurrentBFFFile::ptr(&parser);
        ExpandIncludePath( _m_CurrentBFFFile->GetFileName(), tmp );

        static_cast<LuaNodeGraph &>(nodeGraph).ParseLuaFile( tmp.Get() );
    }
    else if ( varToken->IsVariable() )
    {
        // find variable name
        AStackString< BFFParser::MAX_VARIABLE_NAME_LENGTH > varName;
        bool parentScope = false;
        if ( BFFParser::ParseVariableName( varToken, varName, parentScope ) == false )
        {
            return false; // ParseVariableName will have emitted an error
        }

        const BFFVariable * var = nullptr;
        const BFFStackFrame * const varFrame = ( parentScope )
            ? BFFStackFrame::GetParentDeclaration( varName, BFFStackFrame::GetCurrent()->GetParent(), var )
            : nullptr;

        if ( false == parentScope )
        {
            var = BFFStackFrame::GetVar( varName, nullptr );
        }

        if ( ( parentScope && ( nullptr == varFrame ) ) || ( nullptr == var ) )
        {
            Error::Error_1009_UnknownVariable( varToken, this, varName );
            return false;
        }


        if ( var->GetType() != BFFVariable::VAR_STRING )
        {
            Error::Error_1001_MissingStringStartToken( varToken, this ); // TODO:C Better error message
            return false;
        }

        AStackString<> value( var->GetString() );
        value.Replace( "'", "^'" ); // escape single quotes


        auto _m_CurrentBFFFile = *__BFFParser::m_CurrentBFFFile::ptr(&parser);
        ExpandIncludePath( _m_CurrentBFFFile->GetFileName(), value );

        static_cast<LuaNodeGraph &>(nodeGraph).ParseLuaFile( value.Get() );
    }
    else
    {
        Error::Error_1001_MissingStringStartToken( varToken, this ); // TODO:C Better error message
        return false;
    }

    if ( headerIter.IsAtEnd() == false )
    {
        // TODO:B Error for unexpected junk in header
    }

    return true;
}
