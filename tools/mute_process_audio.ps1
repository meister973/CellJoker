# Mutes one process in the Windows volume mixer for as long as it runs. RPCS3 ignores
# "Audio: Renderer: Null" in a --config override, so a test run would play the game's sound
# on the speakers. Muting the Windows session keeps RPCS3's own WAV dump at full level.
#
#   pwsh -File tools/mute_process_audio.ps1 -ProcessId 1234 [-LogPath mute.log]
param([Parameter(Mandatory = $true)][int]$ProcessId, [string]$LogPath)

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace CellJoker {
    [ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")] class MMDeviceEnumerator {}

    [ComImport, Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IMMDeviceEnumerator {
        int EnumAudioEndpoints(int dataFlow, int stateMask, out IntPtr devices);
        int GetDefaultAudioEndpoint(int dataFlow, int role, out IMMDevice endpoint);
    }

    [ComImport, Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IMMDevice {
        int Activate(ref Guid iid, int context, IntPtr parameters, [MarshalAs(UnmanagedType.IUnknown)] out object result);
    }

    [ComImport, Guid("77AA99A0-1BD6-484F-8BC7-2C654C9A9B6F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IAudioSessionManager2 {
        int GetAudioSessionControl(IntPtr sessionGuid, int flags, out IntPtr control);
        int GetSimpleAudioVolume(IntPtr sessionGuid, int flags, out IntPtr volume);
        int GetSessionEnumerator(out IAudioSessionEnumerator sessions);
    }

    [ComImport, Guid("E2F5BB11-0570-40CA-ACDD-3AA01277DEE8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IAudioSessionEnumerator {
        int GetCount(out int count);
        int GetSession(int index, out IAudioSessionControl2 session);
    }

    // IAudioSessionControl's nine methods come first; only their slots matter here.
    [ComImport, Guid("bfb7ff88-7239-4fc9-8fa2-07c950be9c6d"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IAudioSessionControl2 {
        int GetState(out int state);
        int GetDisplayName(out IntPtr name);
        int SetDisplayName(IntPtr name, IntPtr context);
        int GetIconPath(out IntPtr path);
        int SetIconPath(IntPtr path, IntPtr context);
        int GetGroupingParam(out Guid grouping);
        int SetGroupingParam(IntPtr grouping, IntPtr context);
        int RegisterAudioSessionNotification(IntPtr client);
        int UnregisterAudioSessionNotification(IntPtr client);
        int GetSessionIdentifier(out IntPtr id);
        int GetSessionInstanceIdentifier(out IntPtr id);
        int GetProcessId(out uint processId);
    }

    [ComImport, Guid("87CE5498-68D6-44E5-9215-6DA47EF883D8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface ISimpleAudioVolume {
        int SetMasterVolume(float level, ref Guid context);
        int GetMasterVolume(out float level);
        int SetMute(bool mute, ref Guid context);
        int GetMute(out bool mute);
    }

    public static class SessionMuter {
        const int RENDER = 0, MULTIMEDIA = 1, CLSCTX_ALL = 23;

        // Returns how many sessions of the process are muted on the default output.
        public static int Mute(uint processId) {
            var enumerator = (IMMDeviceEnumerator)new MMDeviceEnumerator();
            IMMDevice device;
            Marshal.ThrowExceptionForHR(enumerator.GetDefaultAudioEndpoint(RENDER, MULTIMEDIA, out device));
            Guid managerId = typeof(IAudioSessionManager2).GUID;
            object managerObject;
            Marshal.ThrowExceptionForHR(device.Activate(ref managerId, CLSCTX_ALL, IntPtr.Zero, out managerObject));
            IAudioSessionEnumerator sessions;
            Marshal.ThrowExceptionForHR(((IAudioSessionManager2)managerObject).GetSessionEnumerator(out sessions));
            int count, muted = 0;
            sessions.GetCount(out count);
            for (int index = 0; index < count; index++) {
                IAudioSessionControl2 session;
                if (sessions.GetSession(index, out session) != 0) continue;
                uint owner;
                if (session.GetProcessId(out owner) != 0 || owner != processId) continue;
                Guid context = Guid.Empty;
                ((ISimpleAudioVolume)session).SetMute(true, ref context);
                muted++;
            }
            return muted;
        }
    }
}
'@

# Sessions appear when the game opens its audio port, and again after a restart of the
# stream, so this keeps muting until the process exits.
$reported = $false
while (Get-Process -Id $ProcessId -ErrorAction SilentlyContinue) {
    try {
        $muted = [CellJoker.SessionMuter]::Mute([uint32]$ProcessId)
        if ($muted -gt 0 -and -not $reported -and $LogPath) {
            Add-Content -Path $LogPath -Value "muted $muted session(s) of process $ProcessId at $(Get-Date -Format o)"
            $reported = $true
        }
    }
    catch { if ($LogPath) { Add-Content -Path $LogPath -Value "mute failed: $($_.Exception.Message)" } }
    Start-Sleep -Milliseconds 50
}
