using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Shapes;
using Microsoft.Win32;
using IOPath = System.IO.Path;

namespace D18 {
 public sealed class Game {
  public string Name,Exe,Dir;
  public override string ToString(){return Name+"\n"+Dir;}
 }

 // Presentation only. Every install / uninstall / validation decision is made by the
 // embedded PowerShell backend through Context.Invoke; this file never touches game files.
 public sealed class SetupWindow:Window {
  const string Version="0.4.0";
  readonly Context ctx; readonly List<Game> games=new List<Game>();
  Dictionary<string,object> options=new Dictionary<string,object>(),meta=new Dictionary<string,object>(),preflight;
  Grid shell,columns;StackPanel body,left,mid,right,stack;ScrollViewer bodyScroll,leftScroll,midScroll,rightScroll;int layout;
  TextBlock notice,nrStatus,warning,installReason;ProgressBar progress;Button installButton;
  string language,themeOverride,page="home",nrClass="",nrHash="",lastLog="",resultTitle="",resultBody="",resultKind="ok",search="";Game game;
  bool busy,dark;string cacheSession=Guid.NewGuid().ToString("N");
  readonly Dictionary<string,object[]> versionLists=new Dictionary<string,object[]>();
  Brush bg,bar,card,well,btn,hover,sel,fg,muted,off,line,priFg,warn,ok,bad;
  static readonly FontFamily Mono=new FontFamily("Cascadia Mono, Consolas, Courier New");

  [DllImport("dwmapi.dll")] static extern int DwmSetWindowAttribute(IntPtr hwnd,int attribute,ref int value,int size);

  public SetupWindow(Context c,string lang,string theme) {
   ctx=c;language=lang=="en"?"en":"zh";themeOverride=theme;
   Title="DLSSNR D18 "+Version;Width=1400;Height=940;MinWidth=640;MinHeight=540;
   WindowStartupLocation=WindowStartupLocation.CenterScreen;ResizeMode=ResizeMode.CanResize;
   FontFamily=new FontFamily("Segoe UI, Microsoft YaHei UI");FontSize=14*TextScale();
   UseLayoutRounding=true;SnapsToDevicePixels=true;
   TextOptions.SetTextFormattingMode(this,TextFormattingMode.Display);
   RestoreBoundsFromSettings();
   options["runtime"]=Json.S(ctx.Settings,"nr");options["optionalUrl"]=Json.S(ctx.Settings,"optionalUrl");
   // The package names where its optional components live; a saved setting may override it.
   if(Json.S(options,"optionalUrl")==""){try{options["optionalUrl"]=Json.S(Json.Read(IOPath.Combine(ctx.Bundle,"optional-components.json")),"url");}catch{}}
   Render();
   SourceInitialized+=(s,e)=>TitleBar(this);
   SizeChanged+=(s,e)=>Reflow();
   Closing+=(s,e)=>{if(busy){e.Cancel=true;MessageBox.Show(T("操作仍在运行，请保持窗口打开。","An operation is running. Keep this window open."),Title);return;}Save();};
   SystemEvents.UserPreferenceChanged+=OnPreference;
   Closed+=(s,e)=>SystemEvents.UserPreferenceChanged-=OnPreference;
   Loaded+=async(s,e)=>{await Scan();if(!String.IsNullOrEmpty(Json.S(options,"runtime")))await ValidateNr();};
  }

  // ---- window state, theme ------------------------------------------------------------
  void OnPreference(object s,UserPreferenceChangedEventArgs e){Dispatcher.BeginInvoke(new Action(()=>{if(!busy){FontSize=14*TextScale();Render();}}));}
  double TextScale(){try{using(var k=Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Accessibility")){return Math.Max(1,Math.Min(2,Convert.ToDouble(k.GetValue("TextScaleFactor",100))/100));}}catch{return 1;}}
  string T(string zh,string en){return language=="en"?en:zh;}
  void RestoreBoundsFromSettings(){try{double w=Convert.ToDouble(ctx.Settings["width"]),h=Convert.ToDouble(ctx.Settings["height"]),x=Convert.ToDouble(ctx.Settings["left"]),y=Convert.ToDouble(ctx.Settings["top"]);var a=new Rect(SystemParameters.VirtualScreenLeft,SystemParameters.VirtualScreenTop,SystemParameters.VirtualScreenWidth,SystemParameters.VirtualScreenHeight);Width=Math.Min(a.Width,Math.Max(MinWidth,w));Height=Math.Min(a.Height,Math.Max(MinHeight,h));Left=Math.Max(a.Left,Math.Min(x,a.Right-Width));Top=Math.Max(a.Top,Math.Min(y,a.Bottom-Height));WindowStartupLocation=WindowStartupLocation.Manual;}catch{}}
  void Save(){var r=WindowState==WindowState.Normal?new Rect(Left,Top,Width,Height):RestoreBounds;ctx.Settings["width"]=r.Width;ctx.Settings["height"]=r.Height;ctx.Settings["left"]=r.Left;ctx.Settings["top"]=r.Top;ctx.Settings["language"]=language;ctx.Settings["nr"]=Json.S(options,"runtime");ctx.Save();}
  static Brush Hex(string v){var b=new SolidColorBrush((Color)ColorConverter.ConvertFromString(v));b.Freeze();return b;}
  void TitleBar(Window w){try{var h=new WindowInteropHelper(w).Handle;if(h==IntPtr.Zero)return;int v=dark?1:0;DwmSetWindowAttribute(h,20,ref v,4);}catch{}}

  // Warm black / white / grey. Hierarchy: page < card; text wells are darker than the card,
  // buttons lighter, so a field and a button can never be mistaken for each other.
  void Palette(){bool light=false;try{using(var k=Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize")){light=Convert.ToInt32(k.GetValue("AppsUseLightTheme",1))!=0;}}catch{light=true;}
   if(themeOverride!="")light=themeOverride=="light";dark=!light;
   bg=Hex(light?"#f4f2ec":"#1a1918");bar=Hex(light?"#ebe8e0":"#131211");card=Hex(light?"#fcfbf8":"#232221");well=Hex(light?"#ffffff":"#171615");
   btn=Hex(light?"#ebe8e0":"#34322f");hover=Hex(light?"#e0dcd2":"#403e3a");sel=Hex(light?"#e6e2d8":"#3a3835");
   fg=Hex(light?"#1f1e1d":"#ecebe6");muted=Hex(light?"#66625b":"#a8a49b");off=Hex(light?"#9a958b":"#77736b");line=Hex(light?"#d6d2c8":"#3a3835");
   priFg=Hex(light?"#ffffff":"#1a1918");warn=Hex(light?"#a86300":"#f0b040");ok=Hex(light?"#1a8a45":"#4fd68a");bad=Hex(light?"#c0392b":"#ff7a6b");
   Background=bg;Foreground=fg;
   var r=new ResourceDictionary();
   r["Bg"]=bg;r["Bar"]=bar;r["Card"]=card;r["Well"]=well;r["Btn"]=btn;r["Hover"]=hover;r["Sel"]=sel;r["Fg"]=fg;r["Muted"]=muted;r["Off"]=off;r["Line"]=line;r["PriFg"]=priFg;
   r.MergedDictionaries.Add((ResourceDictionary)System.Windows.Markup.XamlReader.Parse(Styles));
   Resources=r;TitleBar(this);
  }

  // Every control template is ours, so neither the Windows accent colour nor the default
  // light scroll bars / title bar can leak into the palette.
  const string Styles=@"<ResourceDictionary xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml'>
<Style TargetType='Button'>
 <Setter Property='Foreground' Value='{DynamicResource Fg}'/><Setter Property='Background' Value='{DynamicResource Btn}'/><Setter Property='BorderBrush' Value='{DynamicResource Line}'/>
 <Setter Property='Padding' Value='14,6'/><Setter Property='MinHeight' Value='32'/><Setter Property='HorizontalAlignment' Value='Left'/><Setter Property='FocusVisualStyle' Value='{x:Null}'/>
 <Setter Property='Template'><Setter.Value><ControlTemplate TargetType='Button'>
  <Border x:Name='b' CornerRadius='5' Background='{TemplateBinding Background}' BorderBrush='{TemplateBinding BorderBrush}' BorderThickness='1' Padding='{TemplateBinding Padding}'>
   <ContentPresenter HorizontalAlignment='{TemplateBinding HorizontalContentAlignment}' VerticalAlignment='Center' RecognizesAccessKey='False'/></Border>
  <ControlTemplate.Triggers>
   <Trigger Property='IsMouseOver' Value='True'><Setter TargetName='b' Property='Background' Value='{DynamicResource Hover}'/></Trigger>
   <Trigger Property='IsKeyboardFocused' Value='True'><Setter TargetName='b' Property='BorderBrush' Value='{DynamicResource Fg}'/></Trigger>
   <Trigger Property='IsEnabled' Value='False'><Setter TargetName='b' Property='Opacity' Value='.45'/></Trigger>
  </ControlTemplate.Triggers></ControlTemplate></Setter.Value></Setter>
</Style>
<Style x:Key='Pri' TargetType='Button'>
 <Setter Property='Foreground' Value='{DynamicResource PriFg}'/><Setter Property='Background' Value='{DynamicResource Fg}'/><Setter Property='FontWeight' Value='SemiBold'/>
 <Setter Property='Padding' Value='22,9'/><Setter Property='MinHeight' Value='40'/><Setter Property='HorizontalAlignment' Value='Stretch'/><Setter Property='FocusVisualStyle' Value='{x:Null}'/>
 <Setter Property='Template'><Setter.Value><ControlTemplate TargetType='Button'>
  <Border x:Name='b' CornerRadius='5' Background='{TemplateBinding Background}' Padding='{TemplateBinding Padding}'><ContentPresenter HorizontalAlignment='Center' VerticalAlignment='Center' RecognizesAccessKey='False'/></Border>
  <ControlTemplate.Triggers>
   <Trigger Property='IsMouseOver' Value='True'><Setter TargetName='b' Property='Opacity' Value='.88'/></Trigger>
   <Trigger Property='IsEnabled' Value='False'><Setter TargetName='b' Property='Opacity' Value='.3'/></Trigger>
  </ControlTemplate.Triggers></ControlTemplate></Setter.Value></Setter>
</Style>
<Style x:Key='Link' TargetType='Button'>
 <Setter Property='Foreground' Value='{DynamicResource Muted}'/><Setter Property='Background' Value='Transparent'/><Setter Property='Padding' Value='0,4'/><Setter Property='HorizontalAlignment' Value='Left'/><Setter Property='FocusVisualStyle' Value='{x:Null}'/><Setter Property='Cursor' Value='Hand'/>
 <Setter Property='Template'><Setter.Value><ControlTemplate TargetType='Button'>
  <Border Background='Transparent' Padding='{TemplateBinding Padding}'><ContentPresenter RecognizesAccessKey='False'/></Border>
  <ControlTemplate.Triggers><Trigger Property='IsMouseOver' Value='True'><Setter Property='Foreground' Value='{DynamicResource Fg}'/></Trigger></ControlTemplate.Triggers></ControlTemplate></Setter.Value></Setter>
</Style>
<Style x:Key='Row' TargetType='Button'>
 <Setter Property='Foreground' Value='{DynamicResource Fg}'/><Setter Property='Background' Value='{DynamicResource Card}'/><Setter Property='HorizontalAlignment' Value='Stretch'/><Setter Property='HorizontalContentAlignment' Value='Stretch'/><Setter Property='FocusVisualStyle' Value='{x:Null}'/><Setter Property='Cursor' Value='Hand'/>
 <Setter Property='Template'><Setter.Value><ControlTemplate TargetType='Button'>
  <Border x:Name='b' CornerRadius='6' Background='{TemplateBinding Background}' BorderBrush='{DynamicResource Line}' BorderThickness='1' Padding='14,10'><ContentPresenter RecognizesAccessKey='False'/></Border>
  <ControlTemplate.Triggers>
   <Trigger Property='IsMouseOver' Value='True'><Setter TargetName='b' Property='Background' Value='{DynamicResource Sel}'/></Trigger>
   <Trigger Property='IsKeyboardFocused' Value='True'><Setter TargetName='b' Property='BorderBrush' Value='{DynamicResource Fg}'/></Trigger>
  </ControlTemplate.Triggers></ControlTemplate></Setter.Value></Setter>
</Style>
<Style TargetType='TextBox'>
 <Setter Property='Foreground' Value='{DynamicResource Fg}'/><Setter Property='Background' Value='{DynamicResource Well}'/><Setter Property='BorderBrush' Value='{DynamicResource Line}'/><Setter Property='CaretBrush' Value='{DynamicResource Fg}'/>
 <Setter Property='SelectionBrush' Value='{DynamicResource Muted}'/><Setter Property='Padding' Value='9,6'/><Setter Property='MinHeight' Value='32'/><Setter Property='VerticalContentAlignment' Value='Center'/><Setter Property='FocusVisualStyle' Value='{x:Null}'/>
 <Setter Property='Template'><Setter.Value><ControlTemplate TargetType='TextBox'>
  <Border x:Name='b' CornerRadius='5' Background='{TemplateBinding Background}' BorderBrush='{TemplateBinding BorderBrush}' BorderThickness='1'>
   <Grid Margin='{TemplateBinding Padding}'>
    <TextBlock x:Name='hint' Text='{Binding Tag,RelativeSource={RelativeSource TemplatedParent}}' Foreground='{DynamicResource Off}' Margin='2,0,0,0' VerticalAlignment='Center' TextTrimming='CharacterEllipsis' IsHitTestVisible='False' Visibility='Collapsed'/>
    <ScrollViewer x:Name='PART_ContentHost' VerticalAlignment='Center'/>
   </Grid></Border>
  <ControlTemplate.Triggers>
   <Trigger Property='Text' Value=''><Setter TargetName='hint' Property='Visibility' Value='Visible'/></Trigger>
   <Trigger Property='IsKeyboardFocused' Value='True'><Setter TargetName='b' Property='BorderBrush' Value='{DynamicResource Muted}'/></Trigger>
  </ControlTemplate.Triggers></ControlTemplate></Setter.Value></Setter>
</Style>
<Style TargetType='ComboBoxItem'>
 <Setter Property='Foreground' Value='{DynamicResource Fg}'/><Setter Property='FocusVisualStyle' Value='{x:Null}'/>
 <Setter Property='Template'><Setter.Value><ControlTemplate TargetType='ComboBoxItem'>
  <Border x:Name='b' Background='Transparent' Padding='10,6' CornerRadius='3'><ContentPresenter/></Border>
  <ControlTemplate.Triggers>
   <Trigger Property='IsSelected' Value='True'><Setter TargetName='b' Property='Background' Value='{DynamicResource Btn}'/></Trigger>
   <Trigger Property='IsHighlighted' Value='True'><Setter TargetName='b' Property='Background' Value='{DynamicResource Hover}'/></Trigger>
  </ControlTemplate.Triggers></ControlTemplate></Setter.Value></Setter>
</Style>
<Style TargetType='ComboBox'>
 <Setter Property='Foreground' Value='{DynamicResource Fg}'/><Setter Property='Background' Value='{DynamicResource Btn}'/><Setter Property='BorderBrush' Value='{DynamicResource Line}'/><Setter Property='MinHeight' Value='32'/><Setter Property='FocusVisualStyle' Value='{x:Null}'/>
 <Setter Property='Template'><Setter.Value><ControlTemplate TargetType='ComboBox'>
  <Grid>
   <ToggleButton x:Name='t' Focusable='False' IsChecked='{Binding IsDropDownOpen,RelativeSource={RelativeSource TemplatedParent},Mode=TwoWay}'>
    <ToggleButton.Template><ControlTemplate TargetType='ToggleButton'>
     <Border x:Name='b' Background='{DynamicResource Btn}' BorderBrush='{DynamicResource Line}' BorderThickness='1' CornerRadius='5'>
      <Path Data='M0,0 L4,4 L8,0' Stroke='{DynamicResource Muted}' StrokeThickness='1.5' HorizontalAlignment='Right' VerticalAlignment='Center' Margin='0,0,11,0'/></Border>
     <ControlTemplate.Triggers><Trigger Property='IsMouseOver' Value='True'><Setter TargetName='b' Property='Background' Value='{DynamicResource Hover}'/></Trigger></ControlTemplate.Triggers>
    </ControlTemplate></ToggleButton.Template></ToggleButton>
   <ContentPresenter IsHitTestVisible='False' Content='{TemplateBinding SelectionBoxItem}' ContentTemplate='{TemplateBinding SelectionBoxItemTemplate}' Margin='11,6,30,6' VerticalAlignment='Center'/>
   <Popup x:Name='PART_Popup' Placement='Bottom' VerticalOffset='3' IsOpen='{TemplateBinding IsDropDownOpen}' AllowsTransparency='True' Focusable='False' PopupAnimation='None'>
    <Border Background='{DynamicResource Card}' BorderBrush='{DynamicResource Line}' BorderThickness='1' CornerRadius='5' Padding='3' MinWidth='{Binding ActualWidth,RelativeSource={RelativeSource TemplatedParent}}'>
     <ScrollViewer MaxHeight='300' VerticalScrollBarVisibility='Auto'><ItemsPresenter KeyboardNavigation.DirectionalNavigation='Contained'/></ScrollViewer></Border></Popup>
  </Grid>
  <ControlTemplate.Triggers><Trigger Property='IsEnabled' Value='False'><Setter Property='Opacity' Value='.45'/></Trigger></ControlTemplate.Triggers>
 </ControlTemplate></Setter.Value></Setter>
</Style>
<Style TargetType='CheckBox'>
 <Setter Property='Foreground' Value='{DynamicResource Fg}'/><Setter Property='FocusVisualStyle' Value='{x:Null}'/><Setter Property='Cursor' Value='Hand'/>
 <Setter Property='Template'><Setter.Value><ControlTemplate TargetType='CheckBox'>
  <Grid Background='Transparent'><Grid.ColumnDefinitions><ColumnDefinition Width='Auto'/><ColumnDefinition/></Grid.ColumnDefinitions>
   <Border x:Name='box' Width='18' Height='18' Margin='0,1,10,0' VerticalAlignment='Top' Background='{DynamicResource Well}' BorderBrush='{DynamicResource Muted}' BorderThickness='1' CornerRadius='4'>
    <Path x:Name='mark' Data='M3.5,9 L7.5,13 L14,5' Stroke='{DynamicResource PriFg}' StrokeThickness='2' Visibility='Hidden'/></Border>
   <ContentPresenter Grid.Column='1' VerticalAlignment='Top'/></Grid>
  <ControlTemplate.Triggers>
   <Trigger Property='IsChecked' Value='True'><Setter TargetName='mark' Property='Visibility' Value='Visible'/><Setter TargetName='box' Property='Background' Value='{DynamicResource Fg}'/><Setter TargetName='box' Property='BorderBrush' Value='{DynamicResource Fg}'/></Trigger>
   <Trigger Property='IsKeyboardFocused' Value='True'><Setter TargetName='box' Property='BorderBrush' Value='{DynamicResource Fg}'/></Trigger>
   <Trigger Property='IsEnabled' Value='False'><Setter Property='Opacity' Value='.45'/></Trigger>
  </ControlTemplate.Triggers></ControlTemplate></Setter.Value></Setter>
</Style>
<ControlTemplate x:Key='Thumb' TargetType='Thumb'><Border x:Name='b' CornerRadius='3' Margin='2' Background='{DynamicResource Line}'/>
 <ControlTemplate.Triggers><Trigger Property='IsMouseOver' Value='True'><Setter TargetName='b' Property='Background' Value='{DynamicResource Off}'/></Trigger></ControlTemplate.Triggers></ControlTemplate>
<Style x:Key='Page' TargetType='RepeatButton'><Setter Property='Focusable' Value='False'/><Setter Property='Template'><Setter.Value><ControlTemplate TargetType='RepeatButton'><Border Background='Transparent'/></ControlTemplate></Setter.Value></Setter></Style>
<Style TargetType='ScrollBar'>
 <Setter Property='Background' Value='Transparent'/><Setter Property='Width' Value='10'/><Setter Property='MinWidth' Value='10'/>
 <Setter Property='Template'><Setter.Value><ControlTemplate TargetType='ScrollBar'>
  <Track x:Name='PART_Track' IsDirectionReversed='True'>
   <Track.DecreaseRepeatButton><RepeatButton Style='{StaticResource Page}' Command='ScrollBar.PageUpCommand'/></Track.DecreaseRepeatButton>
   <Track.IncreaseRepeatButton><RepeatButton Style='{StaticResource Page}' Command='ScrollBar.PageDownCommand'/></Track.IncreaseRepeatButton>
   <Track.Thumb><Thumb Template='{StaticResource Thumb}'/></Track.Thumb></Track></ControlTemplate></Setter.Value></Setter>
 <Style.Triggers><Trigger Property='Orientation' Value='Horizontal'>
  <Setter Property='Width' Value='Auto'/><Setter Property='MinWidth' Value='0'/><Setter Property='Height' Value='10'/><Setter Property='MinHeight' Value='10'/>
  <Setter Property='Template'><Setter.Value><ControlTemplate TargetType='ScrollBar'>
   <Track x:Name='PART_Track'>
    <Track.DecreaseRepeatButton><RepeatButton Style='{StaticResource Page}' Command='ScrollBar.PageLeftCommand'/></Track.DecreaseRepeatButton>
    <Track.IncreaseRepeatButton><RepeatButton Style='{StaticResource Page}' Command='ScrollBar.PageRightCommand'/></Track.IncreaseRepeatButton>
    <Track.Thumb><Thumb Template='{StaticResource Thumb}'/></Track.Thumb></Track></ControlTemplate></Setter.Value></Setter>
 </Trigger></Style.Triggers>
</Style>
<Style TargetType='ListBoxItem'>
 <Setter Property='Foreground' Value='{DynamicResource Fg}'/><Setter Property='FocusVisualStyle' Value='{x:Null}'/>
 <Setter Property='Template'><Setter.Value><ControlTemplate TargetType='ListBoxItem'>
  <Border x:Name='b' Background='Transparent' Padding='10,7' CornerRadius='4'><ContentPresenter/></Border>
  <ControlTemplate.Triggers>
   <Trigger Property='IsMouseOver' Value='True'><Setter TargetName='b' Property='Background' Value='{DynamicResource Btn}'/></Trigger>
   <Trigger Property='IsSelected' Value='True'><Setter TargetName='b' Property='Background' Value='{DynamicResource Hover}'/></Trigger>
  </ControlTemplate.Triggers></ControlTemplate></Setter.Value></Setter>
</Style>
<Style TargetType='ListBox'>
 <Setter Property='Background' Value='{DynamicResource Bg}'/><Setter Property='Foreground' Value='{DynamicResource Fg}'/><Setter Property='BorderThickness' Value='0'/><Setter Property='Padding' Value='8'/>
 <Setter Property='Template'><Setter.Value><ControlTemplate TargetType='ListBox'><Border Background='{TemplateBinding Background}' Padding='{TemplateBinding Padding}'><ScrollViewer VerticalScrollBarVisibility='Auto' HorizontalScrollBarVisibility='Disabled'><ItemsPresenter/></ScrollViewer></Border></ControlTemplate></Setter.Value></Setter>
</Style>
</ResourceDictionary>";

  // ---- small building blocks ----------------------------------------------------------
  Thickness Gap(double bottom){return new Thickness(0,0,0,bottom);}
  TextBlock Text(string s,bool faint=false,double size=0){return new TextBlock{Text=s,Foreground=faint?muted:fg,TextWrapping=TextWrapping.Wrap,FontSize=size==0?FontSize:size,Margin=Gap(8)};}
  TextBlock Label(string s){var t=Text(s,true,FontSize-1);t.Margin=new Thickness(0,4,0,5);return t;}
  TextBlock PathText(string s){return new TextBlock{Text=s,Foreground=muted,FontFamily=Mono,FontSize=FontSize-2,TextWrapping=TextWrapping.Wrap,Margin=Gap(0)};}
  // A card: title, optional status chip at the right, then content.
  StackPanel Section(Panel parent,string title,UIElement aside=null){
   var p=new StackPanel();
   if(title!=""){var head=new DockPanel{Margin=Gap(10),LastChildFill=true};
    if(aside!=null){DockPanel.SetDock(aside,Dock.Right);head.Children.Add(aside);}
    head.Children.Add(new TextBlock{Text=title,Foreground=fg,FontWeight=FontWeights.Bold,FontSize=FontSize+2,VerticalAlignment=VerticalAlignment.Center,TextWrapping=TextWrapping.Wrap});
    p.Children.Add(head);}
   parent.Children.Add(new Border{Child=p,Background=card,BorderBrush=line,BorderThickness=new Thickness(1),CornerRadius=new CornerRadius(7),Padding=new Thickness(16,14,16,10),Margin=Gap(12)});
   return p;
  }
  Border Chip(string text,Brush dot){
   var row=new StackPanel{Orientation=Orientation.Horizontal};
   row.Children.Add(new Ellipse{Width=8,Height=8,Fill=dot,VerticalAlignment=VerticalAlignment.Center,Margin=new Thickness(0,1,7,0)});
   row.Children.Add(new TextBlock{Text=text,Foreground=fg,FontSize=FontSize-1,VerticalAlignment=VerticalAlignment.Center});
   return new Border{Child=row,BorderBrush=line,BorderThickness=new Thickness(1),CornerRadius=new CornerRadius(99),Padding=new Thickness(10,2,11,3),VerticalAlignment=VerticalAlignment.Center,HorizontalAlignment=HorizontalAlignment.Left};
  }
  // "label ............ chip" line used for facts the user cannot change.
  void Fact(Panel p,string label,string value,Brush dot){
   var row=new DockPanel{Margin=Gap(8)};var chip=Chip(value,dot);chip.Margin=new Thickness(12,0,0,0);DockPanel.SetDock(chip,Dock.Right);row.Children.Add(chip);
   row.Children.Add(new TextBlock{Text=label,Foreground=fg,TextWrapping=TextWrapping.Wrap,VerticalAlignment=VerticalAlignment.Center});p.Children.Add(row);
  }
  Button Button(Panel p,string label,Action action,string style=""){
   var b=new Button{Content=label,Margin=new Thickness(0,0,8,8)};if(style!="")b.Style=(Style)FindResource(style);
   b.Click+=(s,e)=>{if(!busy)action();};p.Children.Add(b);return b;
  }
  WrapPanel Row(Panel p){var w=new WrapPanel{Margin=Gap(0)};p.Children.Add(w);return w;}
  TextBox Box(Panel p,string key,string placeholder,double width=0){
   if(key=="uiKey"&&Json.S(options,key)=="")options[key]="Insert";
   var b=new TextBox{Text=Json.S(options,key),Tag=placeholder,Margin=Gap(8)};if(width>0){b.Width=width;b.HorizontalAlignment=HorizontalAlignment.Left;}
   System.Windows.Automation.AutomationProperties.SetName(b,placeholder==""?key:placeholder);
   b.TextChanged+=(s,e)=>{options[key]=b.Text;Invalidate();};p.Children.Add(b);return b;
  }
  // Path field with its browse button on the same line.
  void PathRow(Panel p,string key,string placeholder,string browse,Action pick){
   var row=new DockPanel{Margin=Gap(0)};var b=new Button{Content=browse,Margin=new Thickness(8,0,0,8)};b.Click+=(s,e)=>{if(!busy)pick();};DockPanel.SetDock(b,Dock.Right);row.Children.Add(b);
   Box(row,key,placeholder);p.Children.Add(row);
  }
  CheckBox Check(Panel p,string key,string zh,string en,bool value=false,bool rerender=false){
   if(!options.ContainsKey(key))options[key]=value;
   var b=new CheckBox{Content=new TextBlock{Text=T(zh,en),TextWrapping=TextWrapping.Wrap,Foreground=fg},IsChecked=Json.B(options,key),Margin=Gap(9)};
   b.Checked+=(s,e)=>{options[key]=true;Invalidate();if(rerender)Render();};b.Unchecked+=(s,e)=>{options[key]=false;Invalidate();if(rerender)Render();};p.Children.Add(b);return b;
  }
  ComboBox Combo(Panel p,string key,string[] labels,string[] values,Action changed=null){
   var b=new ComboBox{Margin=Gap(8)};foreach(var x in labels)b.Items.Add(x);
   int index=Array.IndexOf(values,Json.S(options,key));b.SelectedIndex=index<0?0:index;if(index<0)options[key]=values[0];
   b.SelectionChanged+=(s,e)=>{options[key]=values[Math.Max(0,b.SelectedIndex)];Invalidate();if(changed!=null)changed();};p.Children.Add(b);return b;
  }
  // Dashed target that says what to drop on it.
  Grid DropZone(UIElement content,Func<string,Task> dropped){
   var edge=new Rectangle{Stroke=off,StrokeThickness=1.2,StrokeDashArray=new DoubleCollection{5,4},RadiusX=7,RadiusY=7,Fill=well};
   var zone=new Grid{Margin=Gap(10),AllowDrop=true,Background=Brushes.Transparent};zone.Children.Add(edge);zone.Children.Add(new Border{Child=content,Padding=new Thickness(16,14,16,8)});
   zone.DragEnter+=(s,e)=>{if(e.Data.GetDataPresent(DataFormats.FileDrop)){edge.Stroke=fg;edge.Fill=sel;}};
   zone.DragLeave+=(s,e)=>{edge.Stroke=off;edge.Fill=well;};
   zone.DragOver+=(s,e)=>{e.Effects=e.Data.GetDataPresent(DataFormats.FileDrop)?DragDropEffects.Copy:DragDropEffects.None;e.Handled=true;};
   zone.Drop+=async(s,e)=>{edge.Stroke=off;edge.Fill=well;e.Handled=true;if(busy||!e.Data.GetDataPresent(DataFormats.FileDrop))return;var a=(string[])e.Data.GetData(DataFormats.FileDrop);if(a.Length>0)await dropped(a[0]);};
   return zone;
  }

  void Invalidate(){preflight=null;options.Remove("preparedPlan");options.Remove("preparedRef");options.Remove("autoPlan");UpdateInstallButton();}
  void UpdateInstallButton(){if(installButton==null||page!="game")return;string why=game==null||game.Exe==""?T("安装需选择游戏 exe；此文件夹可直接卸载。","Choose a game exe to install; this folder can be uninstalled directly."):nrClass==""||nrClass=="CONFLICT"?T("需要先选择并验证一个可用 NR 文件。","Choose and validate a usable NR file first."):nrClass=="UNVERIFIED_COMPATIBLE"&&!Json.B(options,"ackNr")?T("需要先确认使用未验证的 NR 文件。","Confirm the unverified NR file first."):Json.S(options,"apiOrigin")=="guess"&&!Json.B(options,"apiConfirmed")?T("需要先确认图形接口。","Confirm the graphics API first."):"";installButton.IsEnabled=!busy&&why=="";if(installReason!=null){installReason.Text=why;installReason.Visibility=why==""?Visibility.Collapsed:Visibility.Visible;}}

  // ---- frame --------------------------------------------------------------------------
  void Render(){
   // Rebuilding a page must not throw the reader back to the top.
   double keepBody=bodyScroll!=null?bodyScroll.VerticalOffset:0,keepLeft=leftScroll!=null?leftScroll.VerticalOffset:0,keepRight=rightScroll!=null?rightScroll.VerticalOffset:0,keepMid=midScroll!=null?midScroll.VerticalOffset:0;string wasPage=shell==null?"":(string)shell.Tag;
   Palette();installButton=null;installReason=null;warning=null;nrStatus=null;columns=null;leftScroll=null;midScroll=null;rightScroll=null;layout=0;
   shell=new Grid{Background=bg,Tag=page+"|"+(game==null?"":game.Dir)};shell.RowDefinitions.Add(new RowDefinition{Height=GridLength.Auto});shell.RowDefinitions.Add(new RowDefinition{Height=GridLength.Auto});shell.RowDefinitions.Add(new RowDefinition());Content=shell;
   var head=new DockPanel{LastChildFill=true};
   var lang=new Button{Content=language=="en"?"中文":"English",Margin=new Thickness(8,0,0,0)};lang.Click+=(s,e)=>{if(!busy){language=language=="en"?"zh":"en";Render();}};DockPanel.SetDock(lang,Dock.Right);head.Children.Add(lang);
   var about=new Button{Content=T("关于","About"),Margin=new Thickness(8,0,0,0)};about.Click+=(s,e)=>{if(!busy)About();};DockPanel.SetDock(about,Dock.Right);head.Children.Add(about);
   var name=new StackPanel{Orientation=Orientation.Horizontal,VerticalAlignment=VerticalAlignment.Center};
   name.Children.Add(new TextBlock{Text="DLSSNR D18",FontWeight=FontWeights.SemiBold,FontSize=FontSize+2,Foreground=fg});
   name.Children.Add(new TextBlock{Text=Version,Foreground=muted,Margin=new Thickness(10,0,0,0),VerticalAlignment=VerticalAlignment.Bottom,FontSize=FontSize-1});head.Children.Add(name);
   var top=new Border{Child=head,Background=bar,BorderBrush=line,BorderThickness=new Thickness(0,0,0,1),Padding=new Thickness(22,11,22,11)};Grid.SetRow(top,0);shell.Children.Add(top);
   var status=new StackPanel{Margin=new Thickness(22,0,22,0)};
   progress=new ProgressBar{IsIndeterminate=true,Height=2,BorderThickness=new Thickness(0),Background=line,Foreground=fg,Visibility=busy?Visibility.Visible:Visibility.Hidden};
   notice=new TextBlock{Text=busy?T("正在处理，请保持窗口打开…","Working. Keep this window open…"):"",Foreground=muted,FontSize=FontSize-1,Margin=new Thickness(0,6,0,0),MinHeight=4};
   var statusWrap=new StackPanel();statusWrap.Children.Add(progress);status.Children.Add(notice);statusWrap.Children.Add(status);Grid.SetRow(statusWrap,1);shell.Children.Add(statusWrap);
   body=new StackPanel{Margin=new Thickness(22,10,22,18),IsEnabled=!busy};
   bodyScroll=new ScrollViewer{Content=body,VerticalScrollBarVisibility=page=="game"?ScrollBarVisibility.Disabled:ScrollBarVisibility.Auto};Grid.SetRow(bodyScroll,2);shell.Children.Add(bodyScroll);
   if(page=="home")Home();else if(page=="game")GamePage();else if(page=="about")AboutPage();else ResultPage();
   if(wasPage==(string)shell.Tag){var l=leftScroll;var m=midScroll;var r=rightScroll;var b=bodyScroll;Dispatcher.BeginInvoke(new Action(()=>{b.ScrollToVerticalOffset(keepBody);if(l!=null)l.ScrollToVerticalOffset(keepLeft);if(m!=null)m.ScrollToVerticalOffset(keepMid);if(r!=null)r.ScrollToVerticalOffset(keepRight);}),System.Windows.Threading.DispatcherPriority.Loaded);}
  }
  TextBlock Heading(string s){return new TextBlock{Text=s,Foreground=fg,FontSize=FontSize+7,FontWeight=FontWeights.SemiBold,TextWrapping=TextWrapping.Wrap,Margin=Gap(4)};}

  // ---- home: game list ----------------------------------------------------------------
  void Home(){
   body.Children.Add(Heading(T("选择游戏","Choose a game")));
   body.Children.Add(Text(T("已扫描 Steam、Epic、GOG 和 EA。找不到的游戏可以手动添加，或者把 exe / 文件夹直接拖进这个窗口。","Steam, Epic, GOG and EA were scanned. Add a missing game manually, or drop its exe / folder anywhere on this window."),true));
   var tools=new DockPanel{Margin=new Thickness(0,6,0,4)};var buttons=new StackPanel{Orientation=Orientation.Horizontal};DockPanel.SetDock(buttons,Dock.Right);tools.Children.Add(buttons);
   foreach(var item in new[]{new{L=T("添加 exe…","Add exe…"),A=(Action)(()=>PickGame(false))},new{L=T("添加文件夹…","Add folder…"),A=(Action)(()=>PickGame(true))},new{L=T("运行中的程序…","Running program…"),A=(Action)Running},new{L=T("重新扫描","Rescan"),A=(Action)(async()=>await Scan())}}){var b=Button(buttons,item.L,item.A);b.Margin=new Thickness(8,0,0,8);}
   var find=new TextBox{Text=search,Tag=T("搜索游戏名称或路径","Search by game name or path"),Margin=Gap(8)};System.Windows.Automation.AutomationProperties.SetName(find,T("搜索游戏","Search games"));tools.Children.Add(find);body.Children.Add(tools);
   var list=new StackPanel{Margin=Gap(6)};body.Children.Add(list);
   Action refresh=()=>{list.Children.Clear();var shown=games.Where(x=>(x.Name+x.Dir).IndexOf(search,StringComparison.OrdinalIgnoreCase)>=0).ToList();
    foreach(var g in shown){var item=g;
     var row=new DockPanel();var arrow=new TextBlock{Text="›",Foreground=off,FontSize=FontSize+6,Margin=new Thickness(14,-3,0,0),VerticalAlignment=VerticalAlignment.Center};DockPanel.SetDock(arrow,Dock.Right);row.Children.Add(arrow);
     var chip=InstallChip(item);DockPanel.SetDock(chip,Dock.Right);row.Children.Add(chip);
     var names=new StackPanel{VerticalAlignment=VerticalAlignment.Center};names.Children.Add(new TextBlock{Text=item.Name,FontWeight=FontWeights.SemiBold,Foreground=fg,FontSize=FontSize+1,TextTrimming=TextTrimming.CharacterEllipsis,Margin=Gap(2)});var where=PathText(item.Dir);where.TextWrapping=TextWrapping.NoWrap;where.TextTrimming=TextTrimming.CharacterEllipsis;names.Children.Add(where);row.Children.Add(names);
     var button=new Button{Content=row,Style=(Style)FindResource("Row"),Margin=Gap(6)};button.Click+=async(s,e)=>{if(!busy)await SelectGame(item);};list.Children.Add(button);}
    if(shown.Count==0){var empty=new StackPanel();empty.Children.Add(new TextBlock{Text=games.Count==0?T("还没有找到游戏","No games found yet"):T("没有匹配的游戏","No matching games"),Foreground=fg,HorizontalAlignment=HorizontalAlignment.Center,Margin=Gap(4)});empty.Children.Add(new TextBlock{Text=T("把游戏的 exe 或文件夹拖到这里，或用上面的按钮添加。","Drop the game's exe or folder here, or use the buttons above."),Foreground=muted,TextWrapping=TextWrapping.Wrap,TextAlignment=TextAlignment.Center,HorizontalAlignment=HorizontalAlignment.Center,Margin=Gap(6)});
     list.Children.Add(DropZone(empty,p=>{AddGame(p);return Task.FromResult(0);}));}
   };find.TextChanged+=(s,e)=>{search=find.Text;refresh();};refresh();
   NrSection(Section(body,T("我的 NR 文件","My NR file"),NrChip()));
   AllowDrop=true;Drop-=HomeDrop;Drop+=HomeDrop;
  }
  Border InstallChip(Game g){var path=IOPath.Combine(g.Dir,".dlssnr-d18-install.json");if(!File.Exists(path))return Chip(T("未安装","Not installed"),off);
   try{var record=Json.Read(path);string v=Json.S(record,"package_version");return Chip(T("已安装 ","Installed ")+(v==""?Json.S(record,"package_name"):v),ok);}catch{return Chip(T("安装记录需要检查","Record needs attention"),warn);}}
  async void HomeDrop(object s,DragEventArgs e){if(page!="home"||busy||e.Handled||!e.Data.GetDataPresent(DataFormats.FileDrop))return;var paths=(string[])e.Data.GetData(DataFormats.FileDrop);if(paths.Length>0){if(IOPath.GetExtension(paths[0]).Equals(".dll",StringComparison.OrdinalIgnoreCase))await SetNr(paths[0]);else AddGame(paths[0]);}e.Handled=true;}
  async Task Scan(){await Busy(async()=>{var request=new Dictionary<string,object>{{"action","Discover"}};if(ctx.Settings.ContainsKey("scanRoots"))request["scanRoots"]=ctx.Settings["scanRoots"];var r=await Invoke(request);if(Json.B(r,"success")){object data; if(r.TryGetValue("data",out data)){var a=Json.A(data);if(a!=null)foreach(var x in a){var d=x as Dictionary<string,object>;if(d!=null)AddGame(Json.S(d,"path"),false,Json.S(d,"name"));}}}else ShowFailure(r);});Render();}
  void PickGame(bool folder){if(folder){var d=Folder();if(d!="")AddGame(d);}else{var d=FileDialog("Game executable|*.exe");if(d!="")AddGame(d);}}
  string FileDialog(string filter){var d=new OpenFileDialog{Filter=filter};return d.ShowDialog(this)==true?d.FileName:"";}
  string Folder(){using(var d=new System.Windows.Forms.FolderBrowserDialog()){return d.ShowDialog()==System.Windows.Forms.DialogResult.OK?d.SelectedPath:"";}}
  void AddGame(string path,bool render=true,string name=null){if(!File.Exists(path)&&!Directory.Exists(path))return;string exe=Directory.Exists(path)?"":path;string dir=Directory.Exists(path)?IOPath.GetFullPath(path):IOPath.GetDirectoryName(IOPath.GetFullPath(path));if(games.Any(x=>x.Exe==exe&&x.Dir==dir))return;games.Add(new Game{Name=!string.IsNullOrWhiteSpace(name)?name:exe==""?new DirectoryInfo(dir).Name:IOPath.GetFileNameWithoutExtension(exe),Exe=exe,Dir=dir});if(render)Render();}
  Window Dialog(string title,double w,double h){var d=new Window{Owner=this,Title=title,Width=w,Height=h,Background=bg,Foreground=fg,FontFamily=FontFamily,FontSize=FontSize,Resources=Resources,WindowStartupLocation=WindowStartupLocation.CenterOwner,ShowInTaskbar=false};d.SourceInitialized+=(s,e)=>TitleBar(d);return d;}
  void Running(){var pick=Dialog(T("选择正在运行的程序","Choose a running program"),760,480);var root=new DockPanel();var tip=Text(T("双击选择。安装前需要先退出该程序。","Double-click to choose. Exit the program before installing."),true);tip.Margin=new Thickness(18,14,18,4);DockPanel.SetDock(tip,Dock.Top);root.Children.Add(tip);var list=new ListBox{FontFamily=Mono,FontSize=FontSize-1};root.Children.Add(list);pick.Content=root;
   foreach(var p in Process.GetProcesses()){try{if(p.MainWindowHandle!=IntPtr.Zero){string path=p.MainModule.FileName;list.Items.Add(path);}}catch{}}
   list.MouseDoubleClick+=(s,e)=>{if(list.SelectedItem!=null){AddGame((string)list.SelectedItem);pick.Close();}};pick.ShowDialog();}
  public async Task SelectPath(string path){AddGame(path,false);await SelectGame(games.Last());}
  async Task SelectGame(Game g){game=g;var nr=Json.S(options,"runtime");var url=Json.S(options,"optionalUrl");options=new Dictionary<string,object>{{"runtime",nr},{"optionalUrl",url}};preflight=null;cacheSession=Guid.NewGuid().ToString("N");
   await Busy(async()=>{var r=await Invoke(new Dictionary<string,object>{{"action","Meta"},{"game",g.Dir},{"exe",g.Exe}});if(Json.B(r,"success"))meta=Json.D(r,"data");else{meta=new Dictionary<string,object>();ShowFailure(r);}});
   if(Json.S(meta,"exe")!=""){g.Exe=Json.S(meta,"exe");g.Dir=Json.S(meta,"game");}
   options["api"]=Json.S(meta,"api")==""?"None":Json.S(meta,"api");options["apiOrigin"]=Json.S(meta,"origin");options["apiConfirmed"]=Json.S(meta,"origin")!="guess";options["proxy"]=Json.S(meta,"proxy")==""?"dxgi.dll":Json.S(meta,"proxy");options["ref"]="Auto";options["reEngine"]=Json.B(meta,"re_engine");
   SetMissingDefaults();page="game";Render();
  }
  void SetMissingDefaults(){var api=Json.S(options,"api");if(!options.ContainsKey("includeSr"))options["includeSr"]=api!="None"&&Json.S(meta,"sr_version")=="";if(!options.ContainsKey("includeFg"))options["includeFg"]=api=="DX11"&&Json.S(meta,"fg_version")=="";}

  // ---- game page: files on the left, choices and actions on the right ------------------
  void GamePage(){
   Button(body,"‹  "+T("游戏列表","Games"),()=>{page="home";Render();},"Link");
   var title=new DockPanel{Margin=Gap(2)};var state=InstallChip(game);state.Margin=new Thickness(12,4,0,0);DockPanel.SetDock(state,Dock.Right);title.Children.Add(state);title.Children.Add(Heading(game.Name));body.Children.Add(title);
   var where=PathText(game.Dir);where.Margin=Gap(14);body.Children.Add(where);
   // Three independent columns when there is room (files | DLSS and extras | choices and actions),
   // two when narrower, one when narrow. Reflow moves the same panels between scroll viewers.
   columns=new Grid();for(int i=0;i<3;i++)columns.ColumnDefinitions.Add(new ColumnDefinition());
   left=new StackPanel();mid=new StackPanel();right=new StackPanel();stack=new StackPanel();
   leftScroll=new ScrollViewer();midScroll=new ScrollViewer();rightScroll=new ScrollViewer();
   Grid.SetColumn(midScroll,1);Grid.SetColumn(rightScroll,2);columns.Children.Add(leftScroll);columns.Children.Add(midScroll);columns.Children.Add(rightScroll);
   body.Children.Add(columns);

   NrSection(Section(left,T("NR 文件","NR file"),NrChip()));

   var need=Section(left,T("需要的文件","Required files"));
   Fact(need,T("D18 核心与组件","D18 core and components"),T("已包含","Included"),ok);
   object gpu;meta.TryGetValue("gpu",out gpu);string gpuText=ArrayText(gpu);
   var sys=Json.D(meta,"system");object missing;sys.TryGetValue("vc_missing",out missing);string vc=ArrayText(missing);
   Fact(need,T("VC++ 运行库 (x64)","VC++ runtime (x64)"),vc==""?T("已安装","Installed"):T("缺少","Missing"),vc==""?ok:warn);
   if(vc!=""){need.Children.Add(PathText(vc));Check(need,"installVc","安装时一并补齐 VC++（会安装到系统，可能弹出 UAC 或要求重启）","Also install VC++ (installs into Windows; may show UAC or require a restart)",false);
    Button(need,T("现在安装 VC++…","Install VC++ now…"),async()=>{if(Confirm(T("VC++ 运行库会安装到系统。继续？","VC++ will be installed into Windows. Continue?")))await Operation("InstallVC");});}
   need.Children.Add(Label(T("显卡 / 驱动","GPU / driver")));
   need.Children.Add(PathText(gpuText==""?T("未能读取设备信息","Device information unavailable"):gpuText));
   var gpuNote=Text(T("NR 需要 NVIDIA RTX 显卡和较新的驱动。这里只是提示，不会阻止安装。","NR needs an NVIDIA RTX GPU and a recent driver. This is informational and does not block installation."),true,FontSize-1);gpuNote.Margin=new Thickness(0,6,0,6);need.Children.Add(gpuNote);

   var optional=Section(left,T("可选组件","Optional components"));
   Check(optional,"optional","安装 FSR / XeSS 库","Install the FSR / XeSS libraries",false,true);
   optional.Children.Add(Text(T("只用 NR 和 DLSS 时不需要。要让 D18 提供 FSR / XeSS 超分或帧生成时才勾选。","Not needed for NR and DLSS. Tick only if D18 should provide FSR / XeSS upscaling or frame generation."),true,FontSize-1));
   if(Json.B(options,"optional")){PathRow(optional,"optionalPath",T(Json.S(options,"optionalUrl")==""?"可选组件 ZIP 的位置":"留空则安装时自动下载；也可选本地 ZIP",Json.S(options,"optionalUrl")==""?"Location of the optional components ZIP":"Leave empty to download during install, or pick a local ZIP"),T("选择…","Browse…"),()=>SetFile("optionalPath","Optional components ZIP|*.zip",false));
    if(Json.S(options,"optionalUrl")!="")Button(optional,T("现在下载","Download now"),async()=>await DownloadOptional());}

   var loader=Section(mid,T("加载方式","Loading"));
   var apiLine=new DockPanel{Margin=Gap(5)};var origin=Chip(Origin(),Json.S(options,"apiOrigin")=="guess"?warn:ok);DockPanel.SetDock(origin,Dock.Right);apiLine.Children.Add(origin);apiLine.Children.Add(new TextBlock{Text=T("图形接口","Graphics API"),Foreground=muted,FontSize=FontSize-1,VerticalAlignment=VerticalAlignment.Center});loader.Children.Add(apiLine);
   Combo(loader,"api",new[]{"DX12","DX11","Vulkan"},new[]{"None","DX11","Vulkan"},()=>{options["apiOrigin"]="guess";options["apiConfirmed"]=false;options.Remove("includeSr");options.Remove("includeFg");SetMissingDefaults();Render();});
   if(Json.S(options,"apiOrigin")=="guess")Check(loader,"apiConfirmed","我确认这个游戏用的是上面选的图形接口","I confirm the game uses the graphics API selected above");
   loader.Children.Add(Label(T("加载名称","Loader name")));
   Combo(loader,"proxy",new[]{"dxgi.dll","d3d12.dll","winmm.dll","version.dll","dbghelp.dll"},new[]{"dxgi.dll","d3d12.dll","winmm.dll","version.dll","dbghelp.dll"},UpdateWarning);
   warning=Text("",false,FontSize-1);warning.Foreground=warn;loader.Children.Add(warning);UpdateWarning();
   loader.Children.Add(Label(T("菜单键（仅新装；升级保留已有设置）","Menu key (new installs only; upgrades keep yours)")));Box(loader,"uiKey","Insert",140);

   var dlss=Section(mid,T("DLSS 文件","DLSS files"));
   Dlss(dlss,"sr",T("DLSS 超分","DLSS Super Resolution"),Json.S(meta,"sr_version"));
   dlss.Children.Add(new Border{Height=1,Background=line,Margin=new Thickness(0,6,0,12)});
   Dlss(dlss,"fg",T("DLSS 帧生成","DLSS Frame Generation"),Json.S(meta,"fg_version"));
   dlss.Children.Add(Text(T("自动补齐使用 310.9.1.0。游戏已有的文件会保留；替换的文件会先备份，卸载时还原。","Automatic preparation uses 310.9.1.0. Files the game already has are kept; replaced files are backed up and restored on uninstall."),true,FontSize-1));
   var prep=Row(dlss);Button(prep,T("检查缺失项","Check missing files"),async()=>await Operation("AuditDependencies"));Button(prep,T("现在补齐所选项","Prepare selected now"),async()=>{if(Json.B(options,"installVc")&&!Confirm(T("所选 VC++ 会安装到系统。继续？","Selected VC++ will be installed into Windows. Continue?")))return;await Operation("PrepareDependencies");});

   var re=Section(right,"REFramework");
   Check(re,"reEngine","这是 RE Engine 游戏（卡普空）","This is an RE Engine game (Capcom)",Json.B(meta,"re_engine"),true);
   if(Json.B(options,"reEngine")){Combo(re,"ref",new[]{T("自动处理","Automatic"),T("匹配版本","Matched build"),T("最新版本","Latest build"),T("使用游戏目录里已有的","Use the existing one"),T("本地文件，或稍后自己准备","Local file, or prepare later")},new[]{"Auto","Recommended","Latest","Existing","Manual"},Render);
    if(Json.S(options,"ref")=="Manual")PathRow(re,"refPath",T("dinput8.dll 的位置（可留空）","Location of dinput8.dll (optional)"),T("选择…","Browse…"),()=>SetFile("refPath","REFramework DLL|dinput8.dll",false));
    Check(re,"refConfirm","游戏目录里现有的 dinput8.dll 是 REFramework，可以复用或替换","The existing dinput8.dll is REFramework and may be reused or replaced");
    re.Children.Add(Text(T("REFramework 菜单：PgDn。D18 菜单：Insert（或你保存的按键）。","REFramework menu: PgDn. D18 menu: Insert (or your saved key)."),true,FontSize-1));}
   else re.Children.Add(Text(T("RE Engine 游戏需要 REFramework 才能加载 D18。其他游戏不用管。","RE Engine games need REFramework to load D18. Ignore this for other games."),true,FontSize-1));

   var confirm=Section(right,T("安装前确认","Before installing"));
   confirm.Children.Add(Text(T("注入式模组不适用于带反作弊的竞技网游，可能导致无法启动或账号处罚。","Injected mods are unsuitable for competitive online games with anti-cheat and may cause launch failures or account penalties."),true,FontSize-1));
   Check(confirm,"ack","我已了解反作弊风险","I understand the anti-cheat risk");
   if(nrClass=="UNVERIFIED_COMPATIBLE")Check(confirm,"ackNr","我确认使用这个未验证的 NR 文件","I confirm using this unverified NR file");

   var act=Section(right,"");
   installReason=Text("",false,FontSize-1);installReason.Foreground=warn;act.Children.Add(installReason);
   installButton=Button(act,T("安装 / 升级 D18","Install / upgrade D18"),async()=>await Install(),"Pri");installButton.Margin=Gap(10);UpdateInstallButton();
   Check(act,"cleanupCache","安装后清理这次下载的临时文件","Clean up this install's downloaded files",true);
   var more=Row(act);Button(more,T("只检查，不安装","Check only"),async()=>await CheckInstall());Button(more,T("查看日志","View log"),Log);
   act.Children.Add(new Border{Height=1,Background=line,Margin=new Thickness(0,6,0,12)});
   act.Children.Add(Label(T("卸载","Uninstall")));
   var remove=Row(act);Button(remove,T("预览会移除什么","Preview what is removed"),async()=>await Uninstall(true));Button(remove,T("卸载 D18","Uninstall D18"),async()=>await Uninstall(false));
   Check(act,"manual","没有安装记录时，只归档能识别的 D18 文件","Without an install record, archive recognised D18 files only");
   string record=Json.S(options,"stateFile");var pickRecord=Button(act,record==""?T("改用其他备份记录…","Use another backup record…"):T("备份记录：","Backup record: ")+IOPath.GetFileName(IOPath.GetDirectoryName(record)),()=>SetFile("stateFile","install-state.json|install-state.json",false),"Link");
   var disclaimer=Text(T("文件检查通过不代表游戏能正常运行或 NR 效果正确。","Passing file checks does not prove the game runs or that NR looks right."),true,FontSize-1);disclaimer.Margin=new Thickness(0,4,0,4);act.Children.Add(disclaimer);
   Reflow();
  }
  string Origin(){string o=Json.S(options,"apiOrigin");return o=="upgrade"?T("沿用上次安装","From the last install"):o=="known"?T("已知游戏","Known game"):T("猜测，请确认","Guess — please confirm");}
  void Reflow(){if(columns==null||page!="game")return;
   int want=ActualWidth>=1240?3:ActualWidth>=860?2:1;
   if(want!=layout){layout=want;
    leftScroll.Content=null;midScroll.Content=null;rightScroll.Content=null;stack.Children.Clear();
    if(want==3){leftScroll.Content=left;midScroll.Content=mid;rightScroll.Content=right;}
    else{stack.Children.Add(left);stack.Children.Add(mid);if(want==1)stack.Children.Add(right);else rightScroll.Content=right;leftScroll.Content=stack;}
    var star=new GridLength(1,GridUnitType.Star);var none=new GridLength(0);
    columns.ColumnDefinitions[0].Width=want==2?new GridLength(11,GridUnitType.Star):star;columns.ColumnDefinitions[1].Width=want==3?star:none;columns.ColumnDefinitions[2].Width=want==3?star:want==2?new GridLength(9,GridUnitType.Star):none;
    double gap=want==1?0:14;leftScroll.Padding=new Thickness(0,0,gap,0);midScroll.Padding=new Thickness(0,0,gap,0);rightScroll.Padding=new Thickness(0,0,want==1?0:4,0);
    bodyScroll.VerticalScrollBarVisibility=want==1?ScrollBarVisibility.Auto:ScrollBarVisibility.Disabled;
    foreach(var v in new[]{leftScroll,midScroll,rightScroll})v.VerticalScrollBarVisibility=want==1?ScrollBarVisibility.Disabled:ScrollBarVisibility.Auto;
   }
   double room=layout==1?Double.PositiveInfinity:Math.Max(260,ActualHeight-240);leftScroll.MaxHeight=midScroll.MaxHeight=rightScroll.MaxHeight=room;}

  // ---- NR file ------------------------------------------------------------------------
  Border NrChip(){switch(nrClass){case "VERIFIED":return Chip(T("已验证","Verified"),ok);case "ALREADY_PATCHED":return Chip(T("已打过补丁","Already patched"),ok);case "UNVERIFIED_COMPATIBLE":return Chip(T("未验证","Not verified"),warn);case "CONFLICT":return Chip(T("不兼容","Incompatible"),bad);default:return Chip(Json.S(options,"runtime")==""?T("未选择","Not selected"):T("待验证","Not validated"),off);}}
  void NrSection(StackPanel p){
   string file=Json.S(options,"runtime");var inner=new StackPanel();
   Func<Task> choose=async()=>{var f=FileDialog("NR DLL|*.dll");if(f!="")await SetNr(f);};
   if(file==""){
    inner.Children.Add(new TextBlock{Text=T("把 nvngx_dlssnr.dll 拖到这里","Drop nvngx_dlssnr.dll here"),Foreground=fg,FontSize=FontSize+1,HorizontalAlignment=HorizontalAlignment.Center,Margin=Gap(4)});
    inner.Children.Add(new TextBlock{Text=T("或者","or"),Foreground=off,FontSize=FontSize-1,HorizontalAlignment=HorizontalAlignment.Center,Margin=Gap(8)});
    var pick=Button(inner,T("选择文件…","Choose file…"),async()=>await choose());pick.HorizontalAlignment=HorizontalAlignment.Center;pick.Margin=Gap(8);
   }else{
    inner.Children.Add(new TextBlock{Text=IOPath.GetFileName(file),Foreground=fg,FontWeight=FontWeights.SemiBold,TextTrimming=TextTrimming.CharacterEllipsis,Margin=Gap(3)});
    var where=PathText(file);where.Margin=Gap(10);inner.Children.Add(where);
    var row=Row(inner);Button(row,T("更换…","Replace…"),async()=>await choose());Button(row,T("重新验证","Validate again"),async()=>await ValidateNr());
   }
   p.Children.Add(DropZone(inner,SetNr));
   nrStatus=Text(NrMessage(),false,FontSize-1);nrStatus.Foreground=nrClass=="CONFLICT"?bad:nrClass=="VERIFIED"||nrClass=="ALREADY_PATCHED"?muted:nrClass==""?muted:warn;p.Children.Add(nrStatus);
   p.Children.Add(Text(T("选过一次会记住。安装器不附带这个文件，也不提供下载。","Remembered after the first time. Setup neither includes nor downloads this file."),true,FontSize-1));
  }
  string NrMessage(){switch(nrClass){case "VERIFIED":return T("和测试过的文件一致。这不代表它在这个游戏里一定能正常工作。","Matches a tested file. That does not guarantee it works in this game.");case "ALREADY_PATCHED":return T("这个文件已经打过补丁，不会重复修改。只说明补丁齐全，不代表整个文件测试过。","This file is already patched and will not be patched again. That confirms the patches, not the whole file.");case "UNVERIFIED_COMPATIBLE":return T("这个版本没有测试过，但能打上补丁。可能能用，也可能不行，安装前需要额外确认。","This version is untested but accepts the patches. It may or may not work; extra confirmation is required.");case "CONFLICT":return T("和补丁要求对不上，不能使用。请换一个兼容的 NR 文件。","Does not match the patch requirements and cannot be used. Choose a compatible NR file.");default:return Json.S(options,"runtime")==""?T("需要你自己提供 NR 文件。","You need to supply the NR file yourself."):T("还没有验证这个文件。","This file has not been validated yet.");}}
  async Task SetNr(string f){if(!File.Exists(f))return;options["runtime"]=IOPath.GetFullPath(f);options["ackNr"]=false;nrClass="";Invalidate();await ValidateNr();Save();}
  async Task ValidateNr(){await Busy(async()=>{var r=await Invoke(new Dictionary<string,object>{{"action","ValidateNr"},{"runtime",Json.S(options,"runtime")}});nrClass=Json.S(Json.D(r,"data"),"Classification");nrHash=Json.S(Json.D(r,"data"),"SourceSha256");if(!Json.B(r,"success"))lastLog=Json.S(r,"message");});Render();}

  // ---- DLSS SR / FG -------------------------------------------------------------------
  void Dlss(StackPanel p,string kind,string label,string version){
   bool absent=version=="";
   Fact(p,label,absent?T("游戏没有自带","Not shipped with the game"):Clean(version)+(Older(version)?T(" · 较旧"," · older"):""),absent?off:Older(version)?warn:ok);
   Combo(p,kind+"Mode",new[]{absent?T("不安装","Do not install"):T("保留游戏现有的","Keep the game's file"),T("下载指定版本","Download a version"),kind=="sr"?T("使用本地文件","Use a local file"):T("使用本地文件夹","Use a local folder")},new[]{"Keep","Download","Local"},Render);
   string mode=Json.S(options,kind+"Mode");
   if(mode=="Download"){
    object[] entries;if(versionLists.TryGetValue(kind,out entries))VersionCombo(kind,p,entries);
    else Button(p,T("获取版本列表…","Load version list…"),async()=>await Versions(kind));
   }else if(mode=="Local"){
    if(kind=="sr")PathRow(p,"srPath",T("nvngx_dlss.dll 的位置","Location of nvngx_dlss.dll"),T("选择…","Browse…"),()=>SetFile("srPath","DLSS SR DLL|nvngx_dlss.dll",false));
    else PathRow(p,"fgPath",T("帧生成完整配套文件夹的位置","Location of the complete frame generation folder"),T("选择…","Browse…"),()=>{string f=Folder();if(f!=""){options["fgPath"]=f;options["fgMode"]="Local";Invalidate();Render();}});
   }
   if(absent&&mode=="Keep")Check(p,kind=="sr"?"includeSr":"includeFg","游戏缺少这个文件时自动补齐","Add this file automatically because the game lacks it",kind=="sr"?Json.S(options,"api")!="None":Json.S(options,"api")=="DX11");
  }
  static string Clean(string s){return s.Split(' ','-')[0].Replace(',','.');}
  static System.Version Parse(string s){System.Version v;return System.Version.TryParse(Clean(s),out v)?v:new System.Version(0,0);}
  bool Older(string s){var v=Parse(s);return v>new System.Version(0,0)&&v<new System.Version(310,9,1,0);}
  // Newest first; the newest is preselected so "download" never silently means "nothing".
  void VersionCombo(string kind,Panel p,object[] entries){
   var usable=entries.OfType<Dictionary<string,object>>().Where(e=>Json.B(e,"is_signature_valid")&&!Json.B(e,"is_dev_file")).OrderByDescending(e=>Parse(Json.S(e,"version"))).ToList();
   if(usable.Count==0){p.Children.Add(Text(T("没有可用的版本。","No usable versions."),true));return;}
   object current;options.TryGetValue(kind+"Entry",out current);string chosen=current is Dictionary<string,object>?Json.S((Dictionary<string,object>)current,"version"):"";
   var combo=new ComboBox{Margin=Gap(8)};int pick=0;
   for(int i=0;i<usable.Count;i++){string v=Json.S(usable[i],"version");combo.Items.Add(new ComboBoxItem{Content=v+(i==0?T("（最新）"," (latest)"):""),Tag=usable[i]});if(v==chosen)pick=i;}
   combo.SelectedIndex=pick;options[kind+"Entry"]=usable[pick];
   combo.SelectionChanged+=(sender,eventArgs)=>{if(combo.SelectedItem!=null){options[kind+"Entry"]=((ComboBoxItem)combo.SelectedItem).Tag;Invalidate();}};p.Children.Add(combo);
  }
  async Task Versions(string kind){await Busy(async()=>{var r=await Invoke(new Dictionary<string,object>{{"action","Catalog"},{"cacheSession",cacheSession},{"game",game.Dir}});if(!Json.B(r,"success")){ShowFailure(r);return;}var data=Json.D(r,"data");foreach(var pair in new[]{new[]{"sr","dlss"},new[]{"fg","dlss_g"}}){object value;if(data.TryGetValue(pair[1],out value)&&value is System.Collections.IEnumerable)versionLists[pair[0]]=Json.A(value);}});Render();}

  void SetFile(string key,string filter,bool folder){string f=folder?Folder():FileDialog(filter);if(f=="")return;options[key]=f;if(key=="srPath")options["srMode"]="Local";if(key=="refPath")options["ref"]="Manual";Invalidate();Render();}
  async Task DownloadOptional(){string url=Json.S(options,"optionalUrl");Uri u;if(!Uri.TryCreate(url,UriKind.Absolute,out u)||u.Scheme!="https"){MessageBox.Show(T("发布地址尚未配置，请从本地选择组件 ZIP。","The release URL is not configured. Choose a local components ZIP."),Title);return;}if(ctx.Offline){MessageBox.Show(T("离线模式不能下载。","Offline mode cannot download."),Title);return;}
   await Busy(async()=>{var r=await Invoke(Request("DownloadOptional"));if(!Json.B(r,"success")){ShowFailure(r);return;}options["optionalPath"]=Json.S(Json.D(r,"data"),"path");Invalidate();});Render();}

  void UpdateWarning(){if(warning==null||game==null)return;string p=IOPath.Combine(game.Dir,Json.S(options,"proxy"));bool taken=File.Exists(p)&&!File.Exists(IOPath.Combine(game.Dir,".dlssnr-d18-install.json"));warning.Text=taken?T("游戏目录里已经有一个同名文件。安装会先备份它；请确认要用这个加载名称。","The game folder already has a file with this name. Setup backs it up first; make sure this is the loader name you want."):"";warning.Visibility=taken?Visibility.Visible:Visibility.Collapsed;}
  Dictionary<string,object> Request(string action){var r=new Dictionary<string,object>(options);r["action"]=action;if(game!=null){r["game"]=game.Dir;r["exe"]=game.Exe;}r["cacheSession"]=cacheSession;r["originalNr"]=Json.S(options,"runtime");return r;}
  async Task<Dictionary<string,object>> Invoke(Dictionary<string,object> r){var answer=await Task.Run(()=>ctx.Invoke(r));lastLog=Json.S(answer,"message");return answer;}
  async Task Busy(Func<Task> operation){if(busy)return;busy=true;if(notice!=null)notice.Text=T("正在处理，请保持窗口打开…","Working. Keep this window open…");if(progress!=null)progress.Visibility=Visibility.Visible;if(body!=null)body.IsEnabled=false;try{await operation();}catch(Exception e){lastLog=e.ToString();MessageBox.Show(e.Message,Title);}finally{busy=false;if(body!=null)body.IsEnabled=true;if(notice!=null)notice.Text="";if(progress!=null)progress.Visibility=Visibility.Hidden;}}
  void ShowFailure(Dictionary<string,object> r){lastLog=Json.S(r,"message");MessageBox.Show(T("操作未完成：","Operation incomplete: ")+lastLog,Title);}
  bool Confirm(string message){return MessageBox.Show(this,message,Title,MessageBoxButton.YesNo,MessageBoxImage.Question)==MessageBoxResult.Yes;}
  async Task Operation(string action){await Busy(async()=>{var r=await Invoke(Request(action));if(!Json.B(r,"success")){ShowFailure(r);return;}var d=Json.D(r,"data");if(action=="PrepareDependencies"){Invalidate();options["autoPlan"]=Json.S(d,"plan");if(Json.S(d,"ref_path")!="")options["refPath"]=Json.S(d,"ref_path");}resultTitle=T("依赖检查结果","Dependency result");resultBody=DependencyText(d);MessageBox.Show(resultBody,resultTitle);});}
  async Task CheckInstall(){await Busy(async()=>{var r=await Invoke(Request("Check"));if(!Json.B(r,"success")){preflight=null;ShowFailure(r);return;}preflight=Json.D(r,"data");options["preparedPlan"]=Json.S(preflight,"prepared_plan");if(preflight.ContainsKey("prepared_ref"))options["preparedRef"]=preflight["prepared_ref"];nrClass=Json.S(preflight,"runtime_classification");if(nrStatus!=null)nrStatus.Text=NrMessage();lastLog=Json.S(r,"message");MessageBox.Show(T("检查通过。NR 等级：","Check passed. NR classification: ")+nrClass+"\n"+game.Dir+"\n"+(Json.B(preflight,"re_pending")?T("REFramework 尚未就绪。","REFramework is still missing."):"")+"\n"+T("VC++ / 图形依赖缺失项及告警请查看日志。","Review the log for missing VC++ / graphics files and warnings."),Title);});}
  async Task Install(){if(game==null||game.Exe==""){MessageBox.Show(T("安装需选择实际游戏 exe。文件夹可直接用于卸载。","Installation requires the actual game exe. A folder can be used directly for uninstall."),Title);return;}
   if(Json.B(options,"optional")&&Json.S(options,"optionalPath")==""){if(Json.S(options,"optionalUrl")==""){MessageBox.Show(T("请先选择可选组件 ZIP，或取消勾选 FSR / XeSS 库。","Choose the optional components ZIP first, or untick the FSR / XeSS libraries."),Title);return;}await DownloadOptional();if(Json.S(options,"optionalPath")=="")return;}
   await CheckInstall();if(preflight==null)return;
   if(Json.S(options,"apiOrigin")=="guess"&&!Json.B(options,"apiConfirmed")){MessageBox.Show(T("请确认图形接口猜测值。","Confirm the guessed graphics API."),Title);return;}
   if(nrClass=="UNVERIFIED_COMPATIBLE"&&!Json.B(options,"ackNr")){MessageBox.Show(T("请勾选使用未验证 NR 的额外确认。","Tick the extra confirmation for unverified NR."),Title);Render();return;}
   bool prepare=(Json.B(options,"includeSr")&&Json.S(options,"srMode")=="Keep")||(Json.B(options,"includeFg")&&Json.S(options,"fgMode")=="Keep")||Json.B(options,"installVc");
   if(prepare&&!options.ContainsKey("autoPlan")){
    if(Json.B(options,"installVc")&&!Confirm(T("所选 VC++ 会安装到系统。继续？","Selected VC++ will be installed into Windows. Continue?")))return;
    bool ready=false;await Busy(async()=>{var r=await Invoke(Request("PrepareDependencies"));if(!Json.B(r,"success")){ShowFailure(r);return;}var d=Json.D(r,"data");object value;var rows=d.TryGetValue("rows",out value)?Json.A(value):new object[0];var failed=rows.Select(x=>(Dictionary<string,object>)x).Any(x=>(Json.S(x,"id")=="SR"&&Json.B(options,"includeSr")||Json.S(x,"id")=="FG"&&Json.B(options,"includeFg")||Json.S(x,"id")=="VC++"&&Json.B(options,"installVc"))&&new[]{"error","missing","manual","found"}.Contains(Json.S(x,"status")));if(failed){MessageBox.Show(DependencyText(d),Title);return;}options["autoPlan"]=Json.S(d,"plan");if(Json.S(d,"ref_path")!="")options["refPath"]=Json.S(d,"ref_path");ready=true;});if(!ready)return;await CheckInstall();if(preflight==null)return;
   }
   if(Json.B(preflight,"re_pending")){if(!Confirm(T("REFramework 尚未就绪。可安装 D18，但状态将为待准备，不能显示完成。继续？","REFramework is missing. D18 can be installed with a pending status. Continue?")))return;}
   if(!Confirm(T("确认安装 / 升级到以下 exe 目录？将备份所有被替换文件。\n","Install / upgrade into this executable directory? All replaced files will be backed up.\n")+game.Dir+"\n"+(Json.S(options,"api")=="None"?"DX12":Json.S(options,"api"))+" / "+Json.S(options,"proxy")))return;
   await Busy(async()=>{var r=await Invoke(Request("Install"));if(!Json.B(r,"success")){ShowFailure(r);return;}var d=Json.D(r,"data");bool pending=Json.B(d,"re_pending");resultKind=pending?"warn":"ok";resultTitle=pending?T("D18 已安装，REFramework 还没准备好","D18 installed — REFramework still required"):T("已安装，文件校验通过","Installed and files verified");resultBody=T("启动游戏后按 Insert 打开 D18 菜单。NR 开关初始是关的，需要在菜单里打开。\n\n文件校验通过不代表 NR 在这个游戏里一定能正常工作。\n被替换的文件备份在游戏目录的 D18_Backups 里。","After launching the game, press Insert to open the D18 menu. NR starts switched off; turn it on in the menu.\n\nPassing file checks does not prove NR works in this game.\nReplaced files are backed up in D18_Backups inside the game folder.");page="result";});Render();}
  async Task Uninstall(bool preview){if(!preview&&!Confirm(T("卸载所选目录的 D18？备份将保留。\n","Uninstall D18 from this folder? Backups will be retained.\n")+game.Dir))return;await Busy(async()=>{var req=Request("Uninstall");req["planOnly"]=preview;var r=await Invoke(req);if(!Json.B(r,"success")){ShowFailure(r);return;}var d=Json.D(r,"data");if(preview){MessageBox.Show(PlanText(d),T("卸载预览（没有改动）","Uninstall preview (no changes)"));return;}string outcome=Json.S(d,"outcome");resultKind=outcome=="removed"?"ok":"warn";resultTitle=outcome=="nothing"?T("这个文件夹里没有找到 D18 的文件","No D18 files were found in this folder"):outcome=="kept"?T("D18 已移除，有未知文件被保留","D18 removed; unrecognised files were kept"):T("D18 已从这个游戏移除","D18 was removed from this game");resultBody=outcome=="nothing"?T("没有做任何改动。若程序在子目录，请选择那个文件夹。","Nothing was changed. If the program is in a subfolder, choose that folder."):T("原文件已还原或已识别文件归档。卸载前的状态保留在 D18_Backups。","Originals restored or recognised files archived. The pre-uninstall state is retained in D18_Backups.");object kept;d.TryGetValue("kept",out kept);resultBody+="\n"+ArrayText(kept);page="result";});Render();}

  // ---- result, log, about -------------------------------------------------------------
  void ResultPage(){
   var p=Section(body,"");p.Margin=new Thickness(6,10,6,8);
   var head=new StackPanel{Orientation=Orientation.Horizontal,Margin=Gap(10)};head.Children.Add(new Ellipse{Width=12,Height=12,Fill=resultKind=="ok"?ok:warn,VerticalAlignment=VerticalAlignment.Center,Margin=new Thickness(0,2,12,0)});var title=Heading(resultTitle);title.Margin=Gap(0);head.Children.Add(title);p.Children.Add(head);
   if(game!=null){var where=PathText(game.Dir);where.Margin=Gap(12);p.Children.Add(where);}
   var text=Text(resultBody.Trim());text.Margin=Gap(16);p.Children.Add(text);
   var row=Row(p);var back=Button(row,T("回到游戏列表","Back to games"),()=>{page="home";Render();},"Pri");back.HorizontalAlignment=HorizontalAlignment.Left;back.Margin=new Thickness(0,0,10,8);var log=Button(row,T("查看日志","View log"),Log);log.MinHeight=40;
  }
  void Log(){var w=Dialog(T("操作日志","Operation log"),880,600);w.Content=new TextBox{Text=lastLog,IsReadOnly=true,TextWrapping=TextWrapping.Wrap,VerticalScrollBarVisibility=ScrollBarVisibility.Auto,FontFamily=Mono,FontSize=FontSize-1,BorderThickness=new Thickness(0),Padding=new Thickness(14),VerticalContentAlignment=VerticalAlignment.Top,Tag=T("还没有日志。","No log yet.")};w.ShowDialog();}
  void About(){page="about";Render();}
  void AboutPage(){Button(body,"‹  "+T("游戏列表","Games"),()=>{page="home";Render();},"Link");body.Children.Add(Heading(T("关于 DLSSNR D18","About DLSSNR D18")));
   body.Children.Add(Text(T("版本 ","Version ")+Version+T("。安装器不附带也不下载 NVIDIA 的 NR 文件。下面是许可证和第三方声明。",". Setup neither includes nor downloads NVIDIA's NR file. Licences and third-party notices follow."),true));
   var files=new List<string>();foreach(var n in new[]{"LICENSE","THIRD_PARTY_NOTICES.md","ReShade_LICENSE.txt"}){var path=IOPath.Combine(ctx.Bundle,n);if(File.Exists(path))files.Add(path);}
   var licences=IOPath.Combine(ctx.Bundle,"payload","Licenses");if(Directory.Exists(licences))files.AddRange(Directory.GetFiles(licences));
   foreach(var f in files){var s=Section(body,IOPath.GetFileName(f));s.Children.Add(new TextBlock{Text=File.ReadAllText(f),Foreground=muted,FontFamily=Mono,FontSize=FontSize-2,TextWrapping=TextWrapping.Wrap,Margin=Gap(6)});}}
  string DependencyText(Dictionary<string,object> d){object v;var rows=d.TryGetValue("rows",out v)?Json.A(v):null;if(rows!=null)return String.Join("\n\n",rows.Select(x=>{var row=(Dictionary<string,object>)x;return Json.S(row,"id")+": "+Json.S(row,language=="en"?"en":"zh");}));return Json.B(d,"reboot")?T("VC++ 已安装，需要重启 Windows。","VC++ installed; Windows restart required."):T("系统依赖检查已完成，请查看日志。","System dependency check finished; review the log.");}
  string PlanText(Dictionary<string,object> d){object v;var rows=d.TryGetValue("plan",out v)?Json.A(v):null;return rows==null?T("没有可移除的内容。","Nothing to remove."):String.Join("\n",rows.Select(x=>{var row=(Dictionary<string,object>)x;string action=Json.S(row,"Action");return (action=="restore"?T("还原","Restore"):action=="archive"?T("归档","Archive"):T("移除","Remove"))+"  "+Json.S(row,"Relative");}));}
  static string ArrayText(object value){var a=Json.A(value);return String.Join("\n",a.Select(Convert.ToString));}
 }
}
