using System;
using System.IO;
using System.Threading;
using NAudio.Wave;

namespace LocalCallPro;

/// <summary>
/// Hold-to-record voice note recorder.
///
/// Crash root-cause: WaveFileWriter writes a placeholder WAV header on construction,
/// then seeks back and overwrites it with correct lengths only on Dispose().
/// If we call MemoryStream.ToArray() before Dispose(), we get a broken header.
/// If we call it after Dispose(), the stream is already closed.
///
/// Fix: wrap the MemoryStream in a NonClosingWrapper so WaveFileWriter.Dispose()
/// completes the header without closing the stream, then we can safely call ToArray().
/// </summary>
public class VoiceNoteRecorder : IDisposable
{
    private WaveInEvent?    _waveIn;
    private WaveFileWriter? _writer;
    private MemoryStream?   _ms;
    private volatile bool   _recording;
    private readonly object _lock = new();

    public bool IsRecording => _recording;

    public void Start()
    {
        lock (_lock)
        {
            if (_recording) return;
            try
            {
                _ms     = new MemoryStream();
                _writer = new WaveFileWriter(new NonClosingWrapper(_ms), new WaveFormat(16000, 16, 1));
                _waveIn = new WaveInEvent { WaveFormat = new WaveFormat(16000, 16, 1) };
                _waveIn.DataAvailable += OnData;
                _waveIn.StartRecording();   // do this BEFORE setting flag; throws if no mic
                _recording = true;
            }
            catch
            {
                // No microphone available — clean up and return silently
                _waveIn?.Dispose();  _waveIn = null;
                _writer?.Dispose();  _writer = null;
                _ms?.Dispose();      _ms     = null;
            }
        }
    }

    private void OnData(object? sender, WaveInEventArgs e)
    {
        lock (_lock)
        {
            if (_recording && _writer != null)
                _writer.Write(e.Buffer, 0, e.BytesRecorded);
        }
    }

    /// <summary>Stops recording and returns valid WAV bytes. Never throws.</summary>
    public byte[] Stop()
    {
        lock (_lock)
        {
            if (!_recording) return [];
            _recording = false;
        }
        try
        {
            _waveIn?.StopRecording();
            _waveIn?.Dispose();
            _waveIn = null;

            // Dispose the writer: this seeks back, writes correct WAV header, then
            // calls Close() on the NonClosingWrapper (no-op), leaving _ms intact.
            _writer?.Dispose();
            _writer = null;

            // Now _ms has a fully-correct WAV file
            var bytes = _ms?.ToArray() ?? [];
            _ms?.Dispose();
            _ms = null;
            return bytes;
        }
        catch { return []; }
    }

    public void Dispose()
    {
        try { if (_recording) Stop(); } catch { }
        try { _waveIn?.Dispose(); } catch { }
        try { _writer?.Dispose(); } catch { }
        try { _ms?.Dispose();     } catch { }
    }

    // ── NonClosingWrapper ─────────────────────────────────────────────────────
    // Delegates everything to the inner stream but ignores Close()/Dispose()
    // so WaveFileWriter cannot close our MemoryStream before we call ToArray().

    private sealed class NonClosingWrapper(MemoryStream inner) : Stream
    {
        public override bool  CanRead  => inner.CanRead;
        public override bool  CanSeek  => inner.CanSeek;
        public override bool  CanWrite => inner.CanWrite;
        public override long  Length   => inner.Length;
        public override long  Position { get => inner.Position; set => inner.Position = value; }
        public override void  Flush()                                  => inner.Flush();
        public override int   Read(byte[] b, int o, int c)             => inner.Read(b, o, c);
        public override long  Seek(long offset, SeekOrigin origin)     => inner.Seek(offset, origin);
        public override void  SetLength(long value)                    => inner.SetLength(value);
        public override void  Write(byte[] b, int o, int c)            => inner.Write(b, o, c);
        public override void  Close()                                  { /* intentionally suppress */ }
        protected override void Dispose(bool disposing)                { /* intentionally suppress */ }
    }
}
