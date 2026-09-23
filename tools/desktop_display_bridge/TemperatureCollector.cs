// Read-only local sensor endpoint. No fan controls, remote listener or file writes.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Net;
using System.Text;
using System.Threading;
using System.Web.Script.Serialization;
using LibreHardwareMonitor.Hardware;

class TemperatureCollector
{
    static void Read(IHardware hardware, List<object> rows)
    {
        hardware.Update();
        foreach (ISensor sensor in hardware.Sensors)
            if (sensor.SensorType == SensorType.Temperature && sensor.Value.HasValue)
                rows.Add(new { Parent = hardware.Identifier.ToString(), Name = sensor.Name,
                    Value = sensor.Value.Value, Provider = "LibreHardwareMonitor" });
        foreach (IHardware child in hardware.SubHardware) Read(child, rows);
    }

    static int Main(string[] args)
    {
        int parentId;
        if (args.Length != 1 || !Int32.TryParse(args[0], out parentId)) return 2;
        try
        {
            using (Process parent = Process.GetProcessById(parentId))
            using (EventWaitHandle stop = EventWaitHandle.OpenExisting("Local\\SmallDesktopDisplayBridge-9E5B3921-Stop"))
            using (Mutex mutex = new Mutex(false, "Local\\MiniDisplay-TemperatureCollector"))
            {
                if (!mutex.WaitOne(0)) return 0;
                var computer = new Computer { IsCpuEnabled = true, IsGpuEnabled = true };
                var listener = new HttpListener();
                // Use HttpListener directly: the upstream GUI rewrites loopback to '+'.
                listener.Prefixes.Add("http://127.0.0.1:18765/");
                try
                {
                    computer.Open();
                    listener.Start();
                    var serializer = new JavaScriptSerializer();
                    byte[] snapshot = Encoding.UTF8.GetBytes("[]");
                    DateTime sampled = DateTime.MinValue;
                    var pending = listener.BeginGetContext(null, null);
                    while (!parent.HasExited && !stop.WaitOne(0))
                    {
                        if ((DateTime.UtcNow - sampled).TotalSeconds >= 2)
                        {
                            var rows = new List<object>();
                            try { foreach (IHardware hardware in computer.Hardware) Read(hardware, rows); }
                            catch { rows.Clear(); } // Never report stale readings as current.
                            snapshot = Encoding.UTF8.GetBytes(serializer.Serialize(rows));
                            sampled = DateTime.UtcNow;
                        }
                        if (!pending.AsyncWaitHandle.WaitOne(100)) continue;
                        var context = listener.EndGetContext(pending);
                        try
                        {
                            bool allowed = IPAddress.IsLoopback(context.Request.LocalEndPoint.Address) &&
                                IPAddress.IsLoopback(context.Request.RemoteEndPoint.Address) &&
                                context.Request.HttpMethod == "GET" &&
                                context.Request.Url.AbsolutePath == "/sensors";
                            context.Response.StatusCode = allowed ? 200 : 404;
                            context.Response.ContentType = "application/json; charset=utf-8";
                            context.Response.Headers["Cache-Control"] = "no-store";
                            byte[] payload = allowed ? snapshot : Encoding.UTF8.GetBytes("[]");
                            context.Response.ContentLength64 = payload.Length;
                            context.Response.OutputStream.Write(payload, 0, payload.Length);
                        }
                        catch (HttpListenerException) { }
                        catch (System.IO.IOException) { }
                        finally { context.Response.Close(); }
                        pending = listener.BeginGetContext(null, null);
                    }
                    return 0;
                }
                finally { listener.Close(); computer.Close(); mutex.ReleaseMutex(); }
            }
        }
        catch { return 1; }
    }
}
