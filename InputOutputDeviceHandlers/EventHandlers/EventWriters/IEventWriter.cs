﻿using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace InputOutputDeviceHandlers.EventHandlers.EventWriters
{
    public interface IEventWriter<T>
    {
        void WriteEvent(T eventName);
    }
}
