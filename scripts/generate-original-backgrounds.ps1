param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$target=[System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $target | Out-Null
$palettes=@(
    @{Name='genshin';R=65;G=190;B=175},
    @{Name='starrail';R=109;G=129;B=230},
    @{Name='endfield';R=94;G=174;B=209}
)
foreach($palette in $palettes) {
    $file=Join-Path $target ($palette.Name+'.png')
    if(Test-Path -LiteralPath $file) {throw "Refusing to overwrite: $file"}
    $bitmap=New-Object System.Drawing.Bitmap(1280,720)
    $graphics=[System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode=[System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $rect=New-Object System.Drawing.Rectangle(0,0,1280,720)
    $base=[System.Drawing.Color]::FromArgb(9,18,31)
    $accent=[System.Drawing.Color]::FromArgb($palette.R,$palette.G,$palette.B)
    $end=[System.Drawing.Color]::FromArgb([int]($palette.R/5),[int]($palette.G/5),[int]($palette.B/5))
    $gradient=New-Object System.Drawing.Drawing2D.LinearGradientBrush($rect,$base,$end,[single]35)
    try {
        $graphics.FillRectangle($gradient,$rect)
        # Concentric translucent discs approximate the runtime radial vector
        # gradient. All geometry and colors are original code-defined artwork.
        for($radius=400;$radius -ge 8;$radius-=8) {
            $brush=New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(2,$accent))
            try {$graphics.FillEllipse($brush,[single](1062-$radius),[single](230-$radius),[single]($radius*2),[single]($radius*2))} finally {$brush.Dispose()}
        }
        $pen=New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(25,$accent),[single]1)
        try {
            foreach($radius in @(173,245,317)) {$graphics.DrawEllipse($pen,[single](1062-$radius),[single](230-$radius),[single]($radius*2),[single]($radius*2))}
        } finally {$pen.Dispose()}
        $points=[System.Drawing.PointF[]]@(
            [System.Drawing.PointF]::new(0,648),[System.Drawing.PointF]::new(870,374),
            [System.Drawing.PointF]::new(1280,490),[System.Drawing.PointF]::new(1280,720),[System.Drawing.PointF]::new(0,720))
        $brush=New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(105,4,11,21))
        try {$graphics.FillPolygon($brush,$points)} finally {$brush.Dispose()}
        $bitmap.Save($file,[System.Drawing.Imaging.ImageFormat]::Png)
        Write-Output $file
    } finally {$gradient.Dispose();$graphics.Dispose();$bitmap.Dispose()}
}
