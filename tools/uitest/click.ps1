param([int]$x, [int]$y, [string]$action)
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
}
"@
$p = Get-Process tetris -ErrorAction Stop
$h = $p.MainWindowHandle

<#
  THIS DELIBERATELY DOES NOT TOUCH THE REAL FOREGROUND WINDOW.

  The press has to arrive with the app considering itself focused, or SubmitSystemKey drops the
  key-DOWN and the button hit-tests perfectly while nothing happens. The obvious way to arrange
  that is SetForegroundWindow - and it is the wrong way, for two reasons found by using it:

    - It YANKS THE WINDOW IN FRONT OF WHOEVER IS AT THE KEYBOARD, several times per check. That is
      obnoxious on its own, and it also feeds the second problem.
    - It is unreliable by design. Windows refuses foreground changes requested by a background
      process under a pile of conditions, so it succeeds most of the time and silently fails the
      rest - which showed up as a suite that passed one run and failed three checks on the next,
      with flawless hit-testing in the log either way.

  Posting WM_ACTIVATE ourselves does the whole job without either. It is exactly the message
  InputController::HandleMessage listens for to set f_has_focus, so this drives the real code path
  rather than reaching past it, and it leaves the user's actual window arrangement alone.

  It does not weaken what is being tested: that the focus GATE works is proven separately, by
  presses being dropped when f_has_focus is not set.

  STILL WORTH KNOWING: the keyboard is POLLED (GetAsyncKeyState), not message-driven, so anything
  typed at the machine while a suite runs can land on a mapped key and move the piece. Run these
  when you are not using the keyboard, or expect the odd spurious result.
#>
[void][W]::PostMessage($h, 0x0006, [IntPtr]1, [IntPtr]::Zero)   # WM_ACTIVATE, WA_ACTIVE
Start-Sleep -Milliseconds 120

$l = [IntPtr](($y -shl 16) -bor $x)
if     ($action -eq "down") { [void][W]::PostMessage($h, 0x0201, [IntPtr]1, $l) }
elseif ($action -eq "up")   { [void][W]::PostMessage($h, 0x0202, [IntPtr]0, $l) }
elseif ($action -eq "move") { [void][W]::PostMessage($h, 0x0200, [IntPtr]0, $l) }

Write-Output ("posted {0} at {1},{2} hwnd={3}" -f $action, $x, $y, $h)
