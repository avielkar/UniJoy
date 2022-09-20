using SimpleTCP;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

using UnijoyData.Shared.Commands;

namespace UniJoy.Network
{
    class UnityCommandsSender : ICommandsSender
    {
        private SimpleTcpClient _client;
        private readonly int _communicationPort;

        public UnityCommandsSender(int communicationPort)
        {
            _communicationPort = communicationPort;
        }

        public bool TryStart()
        {
            try
            {
                _client = new SimpleTcpClient().Connect("127.0.0.1", _communicationPort);
                _client.Delimiter = (byte)(UnityEngineCommands.CommandsDelimiter);

                return _client?.TcpClient?.Connected ?? false;
            }
            catch (Exception)
            {
                return false;
            }
        }

        public bool TryStop()
        {
            throw new NotImplementedException();
        }

        public bool TrySendCommand(string commandName, string commandValue)
        {
            //_client.WriteLine($"{commandName}{UnityEngineCommands.CommandKeyValueSeperator}{commandValue}");
            return  true;
        }
    }
}
