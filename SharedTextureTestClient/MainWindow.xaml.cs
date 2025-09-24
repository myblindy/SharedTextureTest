using System.Diagnostics;
using System.Windows;

namespace SharedTextureTestClient;

public partial class MainWindow : Window
{
    readonly SharedTextureInterop sharedTextureInterop = new();
    bool backbufferSet;

    public MainWindow()
    {
        InitializeComponent();

        Unloaded += (s, e) => sharedTextureInterop.Dispose();
        d3dImage.IsFrontBufferAvailableChanged += frontBufferAvailableChangedHandler;
    }

    void frontBufferAvailableChangedHandler(object s, DependencyPropertyChangedEventArgs e)
    {
        if (d3dImage.IsFrontBufferAvailable && !backbufferSet)
        {
            Debugger.Launch();
            d3dImage.SetBackBuffer(sharedTextureInterop.WpfTexture);
            backbufferSet = true;
        }
    }

    protected override void OnInitialized(EventArgs e)
    {
        frontBufferAvailableChangedHandler(d3dImage, new());
        sharedTextureInterop.FrameReady += () => Dispatcher.BeginInvoke(() =>
        {
            if (backbufferSet)
            {
                d3dImage.Lock();
                d3dImage.AddDirtyRect(new Int32Rect(0, 0,
                    sharedTextureInterop.Width, sharedTextureInterop.Height));
                d3dImage.Unlock();
            }
        });

        base.OnInitialized(e);
    }
}