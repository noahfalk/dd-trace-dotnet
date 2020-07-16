using Datadog.Trace;
using MessagePack;
using MessagePack.Formatters;
using MessagePack.Resolvers;

namespace Performance.Sandbox
{
    internal class SpanFormatterResolver : IFormatterResolver
    {
        public static readonly IFormatterResolver Instance = new SpanFormatterResolver();

        private static readonly IMessagePackFormatter<Span> Formatter = new SpanMessagePackFormatter();

        private SpanFormatterResolver()
        {
        }

        public IMessagePackFormatter<T> GetFormatter<T>()
        {
            if (typeof(T) == typeof(Span))
            {
                return (IMessagePackFormatter<T>)Formatter;
            }

            return StandardResolver.Instance.GetFormatter<T>();
        }
    }
}
