/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//
#ifndef ANDROID_HARDWARE_IMOBILEBROADBANDSERVICE_H
#define ANDROID_HARDWARE_IMOBILEBROADBANDSERVICE_H

#include <binder/IInterface.h>
#include <utils/String16.h>

namespace android {

// ----------------------------------------------------------------------

class IMobileBroadbandService : public IInterface
{
public:
    DECLARE_META_INTERFACE(MobileBroadbandService);

    /**
     * Is mobile broadband connected
     */
    virtual bool getMobileBroadbandConnected(String16 title) = 0;

    /**
     * connect to mobile broadband
     */
    virtual void connectMobileBroadband(String16 title, String16 apn,  String16 code, String16 user, String16 passwd) = 0;

    /**
     * disconnect from mobile broadband
     */
    virtual void disconnectMobileBroadband(String16 title) = 0;
};

// ----------------------------------------------------------------------

}; // namespace android

#endif // ANDROID_HARDWARE_IMOBILEBROADBANDSERVICE_H
