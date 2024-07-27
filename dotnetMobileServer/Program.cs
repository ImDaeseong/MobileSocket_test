using System;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace dotnetMobileServer
{
    static class Program
    {
        [STAThread]
        static async Task Main()
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new Form1());
        }
    }
}
