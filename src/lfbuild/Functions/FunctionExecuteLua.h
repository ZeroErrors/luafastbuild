#pragma once

#include "Tools/FBuild/FBuildCore/BFF/Functions/Function.h"

class FunctionExecuteLua : public Function
{
public:
    explicit        FunctionExecuteLua();
    inline virtual ~FunctionExecuteLua() override = default;

protected:
    virtual bool AcceptsHeader() const override;
    virtual bool NeedsHeader() const override;
    virtual bool NeedsBody() const override;

    virtual bool ParseFunction( NodeGraph & nodeGraph,
                                BFFParser & parser,
                                const BFFToken * functionNameStart,
                                const BFFTokenRange & headerRange,
                                const BFFTokenRange & bodyRange ) const override;
};
