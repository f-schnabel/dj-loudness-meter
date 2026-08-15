using System.Globalization;
using System.Windows;
using System.Windows.Media;

namespace DjLoudnessMeter.UI;

public sealed class LevelMeterControl : FrameworkElement
{
    private const double BaseMinimumDb = -60.0;
    private const double BaseMaximumDb = 0.0;
    private const double ReleaseDbPerTick = 1.5;
    private static readonly double[] ScaleMarks = [0, -3, -6, -12, -18, -30, -60];
    private static readonly double[] CompactScaleMarks = [0, -6, -12, -30, -60];
    private static readonly Typeface LabelTypeface = new("Segoe UI");
    private double _leftDb = BaseMinimumDb;
    private double _rightDb = BaseMinimumDb;
    private double _holdDb = BaseMinimumDb;
    private double _displayOffsetDb;
    private bool _clipping;

    public LevelMeterControl()
    {
        SnapsToDevicePixels = true;
        UseLayoutRounding = true;
        MinHeight = 64;
    }

    public void SetLevels(float leftDb, float rightDb, float holdDb, bool clipping, bool hasRecentAudio)
    {
        double targetLeft = hasRecentAudio ? ClampDb(leftDb + _displayOffsetDb) : MinimumDb;
        double targetRight = hasRecentAudio ? ClampDb(rightDb + _displayOffsetDb) : MinimumDb;
        _leftDb = Smooth(_leftDb, targetLeft);
        _rightDb = Smooth(_rightDb, targetRight);
        _holdDb = ClampDb(holdDb + _displayOffsetDb);
        _clipping = clipping;
        InvalidateVisual();
    }

    public void SetDisplayOffset(double offsetDb)
    {
        double normalized = double.IsFinite(offsetDb) ? Math.Clamp(offsetDb, 0.0, 30.0) : 0.0;
        if (Math.Abs(normalized - _displayOffsetDb) < 0.001)
        {
            return;
        }

        double difference = normalized - _displayOffsetDb;
        _displayOffsetDb = normalized;
        _leftDb = ClampDb(_leftDb + difference);
        _rightDb = ClampDb(_rightDb + difference);
        _holdDb = ClampDb(_holdDb + difference);
        InvalidateVisual();
    }

    private double MinimumDb => BaseMinimumDb + _displayOffsetDb;

    private double MaximumDb => BaseMaximumDb + _displayOffsetDb;

    protected override void OnRender(DrawingContext drawingContext)
    {
        base.OnRender(drawingContext);
        double width = ActualWidth;
        double height = ActualHeight;
        if (width <= 1 || height <= 1)
        {
            return;
        }

        const double labelWidth = 25;
        const double rightPadding = 5;
        double scaleHeight = Math.Clamp(height * 0.22, 16, 23);
        double rowGap = Math.Clamp(height * 0.06, 4, 12);
        double barHeight = Math.Max(12, (height - scaleHeight - rowGap) / 2.0);
        double meterLeft = labelWidth;
        double meterWidth = Math.Max(1, width - labelWidth - rightPadding);

        DrawScale(drawingContext, meterLeft, meterWidth, scaleHeight);
        DrawChannel(drawingContext, "L", _leftDb, meterLeft, scaleHeight, meterWidth, barHeight);
        DrawChannel(drawingContext, "R", _rightDb, meterLeft, scaleHeight + barHeight + rowGap, meterWidth, barHeight);

        if (_holdDb > MinimumDb)
        {
            double holdX = meterLeft + (DbToPosition(_holdDb) * meterWidth);
            var holdPen = new Pen(new SolidColorBrush(Color.FromRgb(240, 244, 247)), 2);
            drawingContext.DrawLine(holdPen, new Point(holdX, scaleHeight), new Point(holdX, height));
        }

        if (_clipping)
        {
            var clipBrush = new SolidColorBrush(Color.FromRgb(255, 90, 95));
            drawingContext.DrawRoundedRectangle(clipBrush, null, new Rect(width - 13, scaleHeight, 8, height - scaleHeight), 3, 3);
        }
    }

    private void DrawScale(DrawingContext context, double left, double width, double scaleHeight)
    {
        var tickPen = new Pen(new SolidColorBrush(Color.FromRgb(71, 80, 87)), 1);
        double[] marks = width < 360 ? CompactScaleMarks : ScaleMarks;
        foreach (double db in marks)
        {
            double displayDb = db + _displayOffsetDb;
            double x = left + (DbToPosition(displayDb) * width);
            context.DrawLine(tickPen, new Point(x, scaleHeight - 5), new Point(x, scaleHeight));
            string label = displayDb.ToString("+0;-0;0", CultureInfo.InvariantCulture);
            var text = CreateText(label, scaleHeight < 20 ? 8 : 10, Color.FromRgb(143, 154, 163));
            context.DrawText(text, new Point(x - (text.Width / 2), 0));
        }
    }

    private void DrawChannel(
        DrawingContext context,
        string channel,
        double db,
        double left,
        double top,
        double width,
        double height)
    {
        var label = CreateText(channel, height < 22 ? 11 : 14, Color.FromRgb(180, 188, 194));
        context.DrawText(label, new Point(2, top + ((height - label.Height) / 2)));

        var background = new SolidColorBrush(Color.FromRgb(34, 40, 45));
        var border = new Pen(new SolidColorBrush(Color.FromRgb(53, 62, 69)), 1);
        var barBounds = new Rect(left, top, width, height);
        context.DrawRoundedRectangle(background, border, barBounds, 4, 4);

        double filledWidth = DbToPosition(db) * width;
        if (filledWidth <= 0)
        {
            return;
        }

        context.PushClip(new RectangleGeometry(new Rect(left, top, filledWidth, height)));
        DrawSegment(context, left, top, width, height, MinimumDb, -12 + _displayOffsetDb, Color.FromRgb(66, 205, 132));
        DrawSegment(context, left, top, width, height, -12 + _displayOffsetDb, -3 + _displayOffsetDb, Color.FromRgb(246, 195, 68));
        DrawSegment(context, left, top, width, height, -3 + _displayOffsetDb, MaximumDb, Color.FromRgb(255, 90, 95));
        context.Pop();
    }

    private void DrawSegment(
        DrawingContext context,
        double left,
        double top,
        double width,
        double height,
        double fromDb,
        double toDb,
        Color color)
    {
        double segmentLeft = left + (DbToPosition(fromDb) * width);
        double segmentWidth = (DbToPosition(toDb) - DbToPosition(fromDb)) * width;
        context.DrawRectangle(new SolidColorBrush(color), null, new Rect(segmentLeft, top, segmentWidth, height));
    }

    private static FormattedText CreateText(string text, double size, Color color) => new(
        text,
        CultureInfo.InvariantCulture,
        FlowDirection.LeftToRight,
        LabelTypeface,
        size,
        new SolidColorBrush(color),
        1.0);

    private static double Smooth(double current, double target) =>
        target >= current ? target : Math.Max(target, current - ReleaseDbPerTick);

    private double ClampDb(double value) =>
        double.IsFinite(value) ? Math.Clamp(value, MinimumDb, MaximumDb) : MinimumDb;

    private double DbToPosition(double db) => (ClampDb(db) - MinimumDb) / (MaximumDb - MinimumDb);
}
