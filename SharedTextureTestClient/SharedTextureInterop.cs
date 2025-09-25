using Silk.NET.Core.Native;
using Silk.NET.Direct3D11;
using Silk.NET.DXGI;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.IO.Pipes;

namespace SharedTextureTestClient;

public class SharedTextureInterop : INotifyPropertyChanged, IDisposable
{
    public event PropertyChangedEventHandler? PropertyChanged;

    public int Width => 512;
    public int Height => 512;

    public string? Messages
    {
        get;
        private set
        {
            if (value != Messages)
            {
                field = value;
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(Messages)));
            }
        }
    }

    const string frameReadyEventName = "SharedTextureTestFrameReady";
    readonly EventWaitHandle frameReadyEvent =
        new(false, EventResetMode.AutoReset, $"Global\\{frameReadyEventName}");

    readonly D3D11 d3d11 = D3D11.GetApi();
    readonly ComPtr<ID3D11Device5> d3d11Device;
    readonly ComPtr<ID3D11DeviceContext> d3d11DeviceContext;
    readonly ComPtr<ID3D11Texture2D> sharedTexture, wpfTexture;

    public ComPtr<ID3D11Texture2D> SharedTexture => sharedTexture;
    public ComPtr<ID3D11Texture2D> WpfTexture => wpfTexture;

    static readonly Guid allDevicesGuid = new(0xe48ae283, 0xda80, 0x490b, 0x87, 0xe6, 0x43, 0xe9, 0xa9, 0xcf, 0xda, 0x8);

    readonly ComPtr<IDXGIInfoQueue> dxgiInfoQueue;
    readonly ComPtr<ID3D11InfoQueue> d3d11InfoQueue;

    public event Action? NewFrameReady;
    volatile bool stopping;
    readonly Thread frameClockThread;

    public unsafe SharedTextureInterop()
    {
        Debugger.Launch();

        // d3d11 device
        ReadOnlySpan<D3DFeatureLevel> featureLevels = [D3DFeatureLevel.Level111, D3DFeatureLevel.Level110];
        fixed (D3DFeatureLevel* pFeatureLevels = featureLevels)
            SilkMarshal.ThrowHResult(d3d11.CreateDevice(default(ComPtr<IDXGIAdapter>), D3DDriverType.Hardware, 0,
                (uint)(CreateDeviceFlag.BgraSupport | CreateDeviceFlag.Debug),
                pFeatureLevels, 2, D3D11.SdkVersion, ref d3d11Device, null, ref d3d11DeviceContext));

        using (var d3d11Debug = d3d11Device.QueryInterface<ID3D11Debug>())
        {
            d3d11InfoQueue = d3d11Debug.QueryInterface<ID3D11InfoQueue>();
            d3d11InfoQueue.SetBreakOnSeverity(MessageSeverity.Corruption, true);
            d3d11InfoQueue.SetBreakOnSeverity(MessageSeverity.Error, true);
            d3d11InfoQueue.PushEmptyStorageFilter();

            d3d11Debug.ReportLiveDeviceObjects(RldoFlags.Detail | RldoFlags.Summary);
        }

        using (var dxgi = DXGI.GetApi())
        {
            dxgiInfoQueue = dxgi.GetDebugInterface1<IDXGIInfoQueue>(0);
            dxgiInfoQueue.SetBreakOnSeverity(allDevicesGuid, InfoQueueMessageSeverity.Error, true);
            dxgiInfoQueue.SetBreakOnSeverity(allDevicesGuid, InfoQueueMessageSeverity.Corruption, true);
            dxgiInfoQueue.PushEmptyStorageFilter(allDevicesGuid);
        }

        // shared texture
        Texture2DDesc desc = new()
        {
            Width = (uint)Width,
            Height = (uint)Height,
            MipLevels = 1,
            ArraySize = 1,
            Format = Format.FormatB8G8R8A8Unorm,
            SampleDesc = new(1, 0),
            Usage = Usage.Default,
            BindFlags = (uint)(BindFlag.ShaderResource | BindFlag.RenderTarget),
            MiscFlags = (uint)(ResourceMiscFlag.SharedNthandle | ResourceMiscFlag.SharedKeyedmutex),
        };
        SilkMarshal.ThrowHResult(d3d11Device.CreateTexture2D(&desc, null, ref sharedTexture));

        SharedResource sharedHandle;
        using var dxgiTextureResource = sharedTexture.QueryInterface<IDXGIResource1>();
        SilkMarshal.ThrowHResult(dxgiTextureResource.CreateSharedHandle(default(SecurityAttributes*),
            DXGI.SharedResourceWrite | DXGI.SharedResourceRead, default(char*), (void**)&sharedHandle));

        // wpf texture
        desc.MiscFlags = (uint)(ResourceMiscFlag.Shared);
        SilkMarshal.ThrowHResult(d3d11Device.CreateTexture2D(&desc, null, ref wpfTexture));

        using (var pipe = new NamedPipeClientStream(".", "SharedTextureTestPipe",
            PipeDirection.InOut, PipeOptions.Asynchronous | PipeOptions.WriteThrough))
        {
            pipe.Connect();

            using (var pipeWriter = new StreamWriter(pipe, leaveOpen: true))
                pipeWriter.WriteLine($"{(nint)sharedHandle.Handle:D} {Width:D} {Height:D} {frameReadyEventName}");

            using (var pipeReader = new StreamReader(pipe, leaveOpen: true))
                while (string.IsNullOrWhiteSpace(pipeReader.ReadLine())) { }
        }

        // start the frame loop thread
        frameClockThread = new(() =>
        {
            while (!stopping)
            {
                if (frameReadyEvent.WaitOne(100))
                {
                    ComPtr<ID3D11DeviceContext> d3d11DeviceContext = default;
                    d3d11Device.GetImmediateContext(ref d3d11DeviceContext);
                    d3d11DeviceContext.CopyResource(wpfTexture, sharedTexture);
                    d3d11DeviceContext.Flush();
                    d3d11DeviceContext.Dispose();

                    // debug messages
                    for (ulong i = 0; i < d3d11InfoQueue.GetNumStoredMessagesAllowedByRetrievalFilter(); ++i)
                    {
                        nuint messageLength = 0;
                        d3d11InfoQueue.GetMessageA(i, null, ref messageLength);

                        fixed (byte* msgBytes = new byte[messageLength])
                            if (d3d11InfoQueue.GetMessageA(i, (Message*)msgBytes, ref messageLength) == 0)
                                Messages += $"D3D11: {new string((sbyte*)((Message*)msgBytes)->PDescription)}\n";
                    }
                    d3d11InfoQueue.ClearStoredMessages();

                    for (ulong i = 0; i < dxgiInfoQueue.GetNumStoredMessagesAllowedByRetrievalFilters(allDevicesGuid); ++i)
                    {
                        nuint messageLength = 0;
                        dxgiInfoQueue.GetMessageA(allDevicesGuid, i, null, ref messageLength);
                        fixed (byte* msgBytes = new byte[messageLength])
                            if (dxgiInfoQueue.GetMessageA(allDevicesGuid, i, (InfoQueueMessage*)msgBytes, ref messageLength) == 0)
                                Messages += $"DXGI: {new string((sbyte*)((InfoQueueMessage*)msgBytes)->PDescription)}\n";
                    }
                    dxgiInfoQueue.ClearStoredMessages(allDevicesGuid);

                    NewFrameReady?.Invoke();
                }
            }
        })
        { Name = "SharedTextureInterop Frame Clock Thread", IsBackground = true };
        frameClockThread.Start();
    }

    #region IDisposable
    private bool disposedValue;
    protected virtual void Dispose(bool disposing)
    {
        if (!disposedValue)
        {
            if (disposing)
            {
                // managed state
            }

            // unmanaged state
            stopping = true;
            frameClockThread.Join();

            sharedTexture.Dispose();
            d3d11DeviceContext.Dispose();
            d3d11Device.Dispose();
            frameReadyEvent.Dispose();
            d3d11.Dispose();

            disposedValue = true;
        }
    }

    // TODO: override finalizer only if 'Dispose(bool disposing)' has code to free unmanaged resources
    ~SharedTextureInterop()
    {
        // Do not change this code. Put cleanup code in 'Dispose(bool disposing)' method
        Dispose(disposing: false);
    }

    public void Dispose()
    {
        // Do not change this code. Put cleanup code in 'Dispose(bool disposing)' method
        Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }
    #endregion
}
