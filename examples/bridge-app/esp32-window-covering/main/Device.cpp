/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include "Device.h"

#include <cstdio>
#include <lib/support/CHIPMemString.h>
#include <platform/CHIPDeviceLayer.h>

using namespace ::chip::Platform;

Device::Device(const char * szDeviceName, const char * szLocation)
{
    CopyString(mName, sizeof(mName), szDeviceName);
    CopyString(mLocation, sizeof(mLocation), szLocation);
    mState      = kState_Off;
    mReachable  = false;

    mEndpointId      = 0;
    LiftPercentage   = 0;
    mChanged_CB = nullptr;
}

/*
 * 作用: 判断设备是否处于“打开”状态，返回 true 表示设备处于“开”状态。
 */
bool Device::IsOn() const
{
    return mState == kState_On;
}

/**
 * 作用: 判断设备是否可访问，返回 true 表示设备处于可访问状态。
 *  */ 
bool Device::IsReachable() const
{
    return mReachable;
}

/**
 *  作用: 设置设备的可访问性状态，并在状态变化时触发回调函数。
 */
void Device::SetReachable(bool aReachable)
{
    bool changed = (mReachable != aReachable);

    mReachable = aReachable;

    if (aReachable)
    {
        ChipLogProgress(DeviceLayer, "Device[%s]: ONLINE", mName);
    }
    else
    {
        ChipLogProgress(DeviceLayer, "Device[%s]: OFFLINE", mName);
    }

    if (changed && mChanged_CB)
    {
        mChanged_CB(this, kChanged_Reachable);
    }
}
/**
 * 	作用: 设置设备的名称，如果名称变化，则触发回调通知变化。
 */
void Device::SetName(const char * szName)
{
    bool changed = (strncmp(mName, szName, sizeof(mName)) != 0);

    ChipLogProgress(DeviceLayer, "Device[%s]: New Name=\"%s\"", mName, szName);

    CopyString(mName, sizeof(mName), szName);

    if (changed && mChanged_CB)
    {
        mChanged_CB(this, kChanged_Name);
    }
}
/**
 * 	作用: 设置设备的位置，如果位置变化，则触发回调通知变化。
 */
void Device::SetLocation(const char * szLocation)
{
    bool changed = (strncmp(mLocation, szLocation, sizeof(mLocation)) != 0);

    CopyString(mLocation, sizeof(mLocation), szLocation);

    ChipLogProgress(DeviceLayer, "Device[%s]: Location=\"%s\"", mName, mLocation);

    if (changed && mChanged_CB)
    {
        mChanged_CB(this, kChanged_Location);
    }
}
/**
 * 作用: 设置一个回调函数，用于在设备状态或属性（如状态、可访问性、名称、位置）发生变化时被调用。
 */
void Device::SetChangeCallback(DeviceCallback_fn aChanged_CB)
{
    mChanged_CB = aChanged_CB;
}

/**
 *  窗帘
 */
bool Device::WindowsCoverOn()const
{
    return mState == 0;
}
/**
 * 窗帘写
 */
uint16_t Device::GoToLiftPercentage100ths(uint16_t Lift)
{
    bool changed = (mState != 0);
    ChipLogProgress(DeviceLayer, "Set Lift[%s]: %d", mName,Lift);
    LiftPercentage=Lift;
    if (changed && mChanged_CB)
    {
        mChanged_CB(this, kChanged_Windows);
    }
    return LiftPercentage;
}
/**
 * 窗帘读
 */
uint16_t Device::GetCurrentPositionLiftPercent100ths(){return LiftPercentage;}
