using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace Datadog.Trace.ClrProfiler.Integrations.Private
{
    /// <summary>
    /// This type will never be directly used from this assembly
    /// It will be processed by the native bytecode instrumention logic and the SendAsyncWrapper
    /// code will be applied to SendAsync() in System.Net.Http
    /// </summary>
    [TypeRef("System.Net.Http", "System.Net.Http.HttpMessageHandler")]
    internal abstract class HttpMessageHandler
    {
        protected internal abstract Task<HttpResponseMessage> SendAsync(HttpRequestMessage unused, CancellationToken unused2);

        [Intercept("SendAsync")]
        public Task<HttpResponseMessage> SendAsyncWrapper(HttpRequestMessage message, CancellationToken token)
        {
            Console.WriteLine("Muhahaha, gotcha!");
            return null;
        }
    }

#pragma warning disable SA1402 // File may only contain a single class
    [TypeRef("System.Net.Http", "System.Net.Http.HttpRequestMessage")]
    internal class HttpRequestMessage
    {
    }

    [TypeRef("System.Net.Http", "System.Net.Http.HttpResponseMessage")]
    internal class HttpResponseMessage
    {
    }
}
