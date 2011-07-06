/*
 * Copyright (C) 2008 The Android Open Source Project
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

#include <stdint.h>
#include <sys/types.h>

#include <binder/Parcel.h>

#include <hardware_legacy/IMobileBroadbandService.h>

namespace android {

enum {
    GET_MOBILE_BROADBAND_CONNECTED_TRANSACTION = IBinder::FIRST_CALL_TRANSACTION,
    CONNECT_MOBILE_BROADBAND_TRANSACTION,
    DISCONNECT_MOBILE_BROADBAND_TRANSACTION,
};

class BpMobileBroadbandService : public BpInterface<IMobileBroadbandService>
{
public:
    BpMobileBroadbandService(const sp<IBinder>& impl)
        : BpInterface<IMobileBroadbandService>(impl)
    {
    }

    virtual bool getMobileBroadbandConnected(String16 title)
    {
        uint32_t n;
        Parcel data, reply;
        data.writeInterfaceToken(IMobileBroadbandService::getInterfaceDescriptor());
        data.writeString16(title);
        remote()->transact(GET_MOBILE_BROADBAND_CONNECTED_TRANSACTION, data, &reply);
        return reply.readInt32();
    }


    virtual void connectMobileBroadband(String16 title, String16 apn,  String16 code, String16 user, String16 passwd)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IMobileBroadbandService::getInterfaceDescriptor());
        data.writeString16(title);
        data.writeString16(apn);
        data.writeString16(code);
        data.writeString16(user);
        data.writeString16(passwd);
        remote()->transact(CONNECT_MOBILE_BROADBAND_TRANSACTION, data, &reply);
    }

    virtual void disconnectMobileBroadband(String16 title)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IMobileBroadbandService::getInterfaceDescriptor());
        data.writeString16(title);
        remote()->transact(DISCONNECT_MOBILE_BROADBAND_TRANSACTION, data, &reply);
    }

};

IMPLEMENT_META_INTERFACE(MobileBroadbandService, "android.os.IMobileBroadbandService");

};
