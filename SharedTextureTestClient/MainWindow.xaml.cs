using System.Windows;

namespace SharedTextureTestClient;

/// <summary>
/// Interaction logic for MainWindow.xaml
/// </summary>
public partial class MainWindow : Window
{
    readonly SharedTextureInterop sharedTextureInterop = new();

    public MainWindow()
    {
        InitializeComponent();
    }
}