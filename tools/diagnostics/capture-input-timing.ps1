param(
    [Parameter(Mandatory=$true)][string]$OutputPath
)

$source = @"
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

public static class CoralDebugCapture
{
    const uint PAGE_READWRITE = 0x04;
    const uint FILE_MAP_READ = 0x0004;
    const uint WAIT_OBJECT_0 = 0;
    const uint WAIT_TIMEOUT = 258;

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr CreateEventW(IntPtr attributes, bool manualReset, bool initialState, string name);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr CreateFileMappingW(IntPtr file, IntPtr attributes, uint protection, uint sizeHigh, uint sizeLow, string name);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr MapViewOfFile(IntPtr mapping, uint access, uint offsetHigh, uint offsetLow, UIntPtr bytes);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool SetEvent(IntPtr handle);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
    [DllImport("kernel32.dll")]
    static extern bool UnmapViewOfFile(IntPtr address);
    [DllImport("kernel32.dll")]
    static extern bool CloseHandle(IntPtr handle);

    public static void Run(string path)
    {
        IntPtr ready = CreateEventW(IntPtr.Zero, false, false, "DBWIN_BUFFER_READY");
        IntPtr data = CreateEventW(IntPtr.Zero, false, false, "DBWIN_DATA_READY");
        IntPtr mapping = CreateFileMappingW(new IntPtr(-1), IntPtr.Zero, PAGE_READWRITE, 0, 4096, "DBWIN_BUFFER");
        if (ready == IntPtr.Zero || data == IntPtr.Zero || mapping == IntPtr.Zero)
            throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        IntPtr view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, new UIntPtr(4096));
        if (view == IntPtr.Zero)
            throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        try
        {
            using (var writer = new StreamWriter(path, true, new UTF8Encoding(false)))
            {
                writer.AutoFlush = true;
                Console.WriteLine("Capturing [coral_input_timing] to " + path);
                while (true)
                {
                    if (!SetEvent(ready)) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
                    uint status = WaitForSingleObject(data, 1000);
                    if (status == WAIT_TIMEOUT) continue;
                    if (status != WAIT_OBJECT_0) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
                    int pid = Marshal.ReadInt32(view);
                    byte[] bytes = new byte[4092];
                    Marshal.Copy(IntPtr.Add(view, 4), bytes, 0, bytes.Length);
                    int end = Array.IndexOf(bytes, (byte)0);
                    if (end < 0) end = bytes.Length;
                    string message = Encoding.Default.GetString(bytes, 0, end).TrimEnd('\r', '\n');
                    if (!message.Contains("[coral_input_timing]")) continue;
                    string line = DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff") + " dbwin_pid=" + pid + " " + message;
                    writer.WriteLine(line);
                    Console.WriteLine(line);
                }
            }
        }
        finally
        {
            UnmapViewOfFile(view);
            CloseHandle(mapping);
            CloseHandle(data);
            CloseHandle(ready);
        }
    }
}
"@

Add-Type -TypeDefinition $source -Language CSharp
[CoralDebugCapture]::Run([IO.Path]::GetFullPath($OutputPath))
