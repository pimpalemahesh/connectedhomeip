/*
 *   Copyright (c) 2024 Project CHIP Authors
 *   All rights reserved.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 *
 */

#include "../common/CHIPCommandBridge.h"

#include <lib/core/Optional.h>

/**
 * This command dump the memory graph of the program.
 */

class DumpMemoryGraphCommand : public CHIPCommandBridge
{
public:
    DumpMemoryGraphCommand() : CHIPCommandBridge("dump-graph")
    {
        AddArgument("filepath", &mFilePath,
                    "An optional filepath to save the memory graph to. Defaults to 'darwin-framework-tool.memgraph");
    }

    /////////// CHIPCommandBridge Interface /////////
    CHIP_ERROR RunCommand() override;
    chip::System::Clock::Timeout GetWaitDuration() const override { return chip::System::Clock::Milliseconds32(0); }

private:
    chip::Optional<char *> mFilePath;
};
