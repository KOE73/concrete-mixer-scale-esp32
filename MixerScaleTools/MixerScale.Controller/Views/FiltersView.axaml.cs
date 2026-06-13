using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Markup.Xaml;
using Avalonia.Media;
using MixerScale.Controller.ViewModels;

namespace MixerScale.Controller.Views;

/// <summary>
/// Таблица MA-фильтров. Строится динамически через код (cross-tab с переменным числом колонок-единиц).
/// Перестраивается полностью только при изменении данных (Rows изменился).
/// </summary>
internal sealed partial class FiltersView : UserControl
{
    private ContentControl _host = null!;
    private FiltersViewModel? _vm;

    public FiltersView()
    {
        AvaloniaXamlLoader.Load(this);
        _host = this.FindControl<ContentControl>("FiltersTableHost")!;
    }

    protected override void OnDataContextChanged(EventArgs e)
    {
        base.OnDataContextChanged(e);

        if (_vm is not null)
        {
            _vm.PropertyChanged -= OnVmPropertyChanged;
        }

        _vm = DataContext as FiltersViewModel;

        if (_vm is not null)
        {
            _vm.PropertyChanged += OnVmPropertyChanged;
            RebuildTable();
        }
        else
        {
            _host.Content = null;
        }
    }

    private void OnVmPropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
    {
        if (e.PropertyName is nameof(FiltersViewModel.Rows) or nameof(FiltersViewModel.UnitHeaders))
        {
            RebuildTable();
        }
    }

    private void RebuildTable()
    {
        if (_vm is null || _vm.Rows.Count == 0)
        {
            _host.Content = null;
            return;
        }

        var rows      = _vm.Rows;
        var unitCols  = _vm.UnitHeaders;           // ["raw", "kg", "т", ...]
        var colCount  = 1 + unitCols.Count;        // checkbox + name + unitCols

        // ColumnDefinitions: Auto для всех колонок для максимального сжатия
        var colDefs = "Auto" + string.Concat(Enumerable.Repeat(",Auto", unitCols.Count));
        var table = new Grid
        {
            ColumnDefinitions = new ColumnDefinitions(colDefs),
            RowDefinitions    = new RowDefinitions("Auto" + string.Concat(Enumerable.Repeat(",Auto", rows.Count))),
            ColumnSpacing     = 8,
            RowSpacing        = 2
        };

        // Заголовок строка 0
        for (var c = 0; c < unitCols.Count; c++)
        {
            table.Children.Add(HeaderCell(unitCols[c], 0, 1 + c));
        }

        // Строки фильтров
        for (var r = 0; r < rows.Count; r++)
        {
            var row = rows[r];
            var gridRow = r + 1;

            // Имя
            table.Children.Add(DataCell(row.Name, gridRow, 0, bold: true));

            // raw
            table.Children.Add(DataCell(row.RawSum, gridRow, 1));

            // Значения единиц
            table.Children.Add(DataCell(row.Kg, gridRow, 2));

            for (var c = 0; c < row.Units.Count; c++)
            {
                table.Children.Add(DataCell(row.Units[c], gridRow, 3 + c));
            }
        }

        _host.Content = table;
    }

    private static TextBlock HeaderCell(string text, int row, int col) =>
        SetPos(new TextBlock
        {
            Text           = text,
            FontFamily     = FontFamily.Parse("Consolas"),
            FontWeight     = FontWeight.SemiBold,
            Foreground     = SolidColorBrush.Parse("#808080"), // TextSecondaryBrush
            VerticalAlignment = VerticalAlignment.Center,
            Margin         = new Thickness(0)
        }, row, col);

    private static TextBlock DataCell(string text, int row, int col,
        bool bold = false, bool alignRight = false) =>
        SetPos(new TextBlock
        {
            Text           = text,
            FontFamily     = FontFamily.Parse("Consolas"),
            FontWeight     = bold ? FontWeight.SemiBold : FontWeight.Normal,
            TextAlignment  = TextAlignment.Left,
            VerticalAlignment = VerticalAlignment.Center,
            Margin         = new Thickness(0)
        }, row, col);

    private static T SetPos<T>(T control, int row, int col) where T : Control
    {
        Grid.SetRow(control, row);
        Grid.SetColumn(control, col);
        return control;
    }
}
