using System.Windows;
using System.Windows.Interop;

namespace SharedTextureTestClient;

public partial class MainWindow : Window
{
    readonly SharedTextureInterop sharedTextureInterop = new();

    public MainWindow()
    {
        InitializeComponent();

        d3dImage.IsFrontBufferAvailableChanged += (s, e) =>
        {
            if (d3dImage.IsFrontBufferAvailable)
            {
                d3dImage.Lock();
                d3dImage.SetBackBuffer(D3DResourceType.IDirect3DSurface9, sharedTextureInterop.D3D9Texture);
                d3dImage.Unlock();
            }
        };
    }

    protected override void OnInitialized(EventArgs e)
    {
        sharedTextureInterop.FrameReady += () => Dispatcher.BeginInvoke(() =>
        {
            d3dImage.AddDirtyRect(new Int32Rect(0, 0,
                sharedTextureInterop.Width, sharedTextureInterop.Height));
        });
    }
}