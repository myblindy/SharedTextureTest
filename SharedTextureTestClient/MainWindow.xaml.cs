using System.Collections.ObjectModel;
using System.Windows;

namespace SharedTextureTestClient;

public partial class MainWindow : Window
{
    public SharedTextureInterop SharedTextureInterop { get; } = new();
    bool backbufferSet;

    public MainWindow()
    {
        InitializeComponent();

        Unloaded += (s, e) => SharedTextureInterop.Dispose();
        d3dImage.IsFrontBufferAvailableChanged += frontBufferAvailableChangedHandler;
    }

    void frontBufferAvailableChangedHandler(object s, DependencyPropertyChangedEventArgs e)
    {
        if (d3dImage.IsFrontBufferAvailable && !backbufferSet)
        {
            //Debugger.Launch();
            d3dImage.SetBackBuffer(SharedTextureInterop.WpfTexture);
            backbufferSet = true;
        }
    }

    private unsafe void OnLoaded(object sender, RoutedEventArgs e)
    {
        frontBufferAvailableChangedHandler(d3dImage, new());
        SharedTextureInterop.NewFrameReady += () => Dispatcher.BeginInvoke(() =>
        {
            if (backbufferSet)
            {
                d3dImage.Lock();
                d3dImage.AddDirtyRect(new Int32Rect(0, 0,
                    SharedTextureInterop.Width, SharedTextureInterop.Height));
                d3dImage.Unlock();
            }
        });
    }
}