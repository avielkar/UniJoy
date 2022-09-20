using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace MoogController
{
    public static class MoogController
    {
#if DEBUG
        
        [DllImport(@"C:\Users\user\source\repos\UniJoy\MoogInterfeace\Debug\MoogInterfeace.dll")]
        public static extern void Connect();
        [DllImport(@"C:\Users\user\source\repos\UniJoy\MoogInterfeace\Debug\MoogInterfeace.dll")]
        public static extern void Engage();
        [DllImport(@"C:\Users\user\source\repos\UniJoy\MoogInterfeace\Debug\MoogInterfeace.dll")]
        public static extern void Disengage();
        [DllImport(@"C:\Users\user\source\repos\UniJoy\MoogInterfeace\Debug\MoogInterfeace.dll")]
        public static extern void Disconnect();
        [DllImport(@"C:\Users\user\source\repos\UniJoy\MoogInterfeace\Debug\MoogInterfeace.dll", CallingConvention = CallingConvention.Cdecl)]
        public static extern void SendPosition(double surge, double heave, double lateral, double yaw, double roll, double pitch);
        /*
        [DllImport(@"C:\Users\user\Desktop\Michael\dev\UniJoy\MoogInterfeace\Debug\MoogInterfeace.dll")]
        public static extern void Connect();
        [DllImport(@"C:\Users\user\Desktop\Michael\dev\UniJoy\MoogInterfeace\Debug\MoogInterfeace.dll")]
        public static extern void Engage();
        [DllImport(@"C:\Users\user\Desktop\Michael\dev\UniJoy\MoogInterfeace\Debug\MoogInterfeace.dll")]
        public static extern void Disengage();
        [DllImport(@"C:\Users\user\Desktop\Michael\dev\UniJoy\MoogInterfeace\Debug\MoogInterfeace.dll")]
        public static extern void Disconnect();
        [DllImport(@"C:\Users\user\Desktop\Michael\dev\UniJoy\MoogInterfeace\Debug\MoogInterfeace.dll", CallingConvention = CallingConvention.Cdecl)]
        public static extern void SendPosition(double surge, double heave, double lateral, double yaw, double roll, double pitch);
        */
#else


        [DllImport(@"C:\Users\user\source\repos\UniJoy\MoogInterfeace\Release\MoogInterfeace.dll")]
                public static extern void Connect();
                [DllImport(@"C:\Users\user\source\repos\UniJoy\MoogInterfeace\Release\MoogInterfeace.dll")]
                public static extern void Engage();
                [DllImport(@"C:\Users\user\source\repos\UniJoy\MoogInterfeace\Release\MoogInterfeace.dll")]
                public static extern void Disengage();
                [DllImport(@"C:\Users\user\source\repos\UniJoy\MoogInterfeace\Release\MoogInterfeace.dll")]
                public static extern void Disconnect();
                [DllImport(@"C:\Users\user\source\repos\UniJoy\MoogInterfeace\Release\MoogInterfeace.dll" , CallingConvention = CallingConvention.Cdecl)]
                public static extern void SendPosition(double surge, double heave, double lateral, double yaw, double roll, double pitch);
#endif
    }
}
