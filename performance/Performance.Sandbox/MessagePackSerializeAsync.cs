using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;
using BenchmarkDotNet.Attributes;
using Datadog.Trace;
using TraceMessagePack = Datadog.Trace.Agent.MessagePack;

namespace Performance.Sandbox
{
    [MemoryDiagnoser]
    public class MessagePackSerializeAsync
    {
        [Params(10, 100, 1000, 10000)]
        public int NumTraces;
        private const int NumSpansInTrace = 100;

        private readonly FormatterResolverWrapper _messagePackformatterResolver = new FormatterResolverWrapper(SpanFormatterResolver.Instance);
        private readonly TraceMessagePack.FormatterResolverWrapper _vendoredFormatterResolver = new TraceMessagePack.FormatterResolverWrapper(TraceMessagePack.SpanFormatterResolver.Instance);

        private readonly Span[][] _traces;
        private readonly MemoryStream _memoryStream;

        public MessagePackSerializeAsync()
        {
            var finishedTraces = new List<Span[]>();
            for (int i = 0; i < NumTraces; i++)
            {
                var trace = new Stack<Span>();
                var finishedTrace = new List<Span>();

                // Start spans
                for (int j = 0; j < NumSpansInTrace; j++)
                {
                    trace.Push(Tracer.Instance.StartSpan("test")); 
                }

                // Finish spans in reverse order
                for (int j = 0; j < NumSpansInTrace; j++)
                {
                    var span = trace.Pop();
                    span.Finish();
                    finishedTrace.Add(span);
                }

                finishedTraces.Add(finishedTrace.ToArray());
            }

            _traces = finishedTraces.ToArray();
            _memoryStream = new MemoryStream();
        }

        [Benchmark]
        public async Task NuGetSerializeAsync()
        {
            _memoryStream.Position = 0;
            await MessagePack.MessagePackSerializer.SerializeAsync(_memoryStream, _traces, _messagePackformatterResolver);
        }

        [Benchmark]
        public async Task VendoredSerializeAsync()
        {
            _memoryStream.Position = 0;
            await Datadog.Trace.Vendors.MessagePack.MessagePackSerializer.SerializeAsync(_memoryStream, _traces, _vendoredFormatterResolver);
        }
    }
}
