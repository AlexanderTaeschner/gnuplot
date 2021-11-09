namespace Webify
{
    using System;
    using System.Diagnostics;
    using System.IO;
    using System.Text.RegularExpressions;
    using System.Threading;
    using System.Web;

    class Program
    {
        static void Main(string[] args)
        {
            if (args.Length < 3)
            {
                Console.WriteLine("Invalid number of parameters!");
            }

            var gnuplotPath = args[0];
            var demoPath = args[1];
            var htmlPath = args[2];
            if (args.Length == 3)
            {
                var files = "airfoil.html animation.html approximate.html armillary.html array.html arrowstyle.html barchart_art.html binary.html bins.html bivariat.html boxplot.html boxclusters.html candlesticks.html circles.html cities.html columnhead.html contours.html convex_hull.html custom_contours.html controls.html custom_key.html dashtypes.html datastrings.html dgrid3d.html discrete.html electron.html ellipse.html enhanced_utf8.html epslatex.html errorbars.html fenceplot.html fillbetween.html fillcrvs.html fillstyle.html finance.html fit.html hidden.html hidden2.html hidden_compare.html histograms.html histograms2.html histerror.html gantt.html image.html image2.html imageNaN.html index.html iterate.html jitter.html keyentry.html label_stacked_histograms.html layout.html lines_arrows.html linkedaxes.html map_projection.html margins.html monotonic_spline.html multiaxis.html multiplt.html named_palettes.html nokey.html nonlinear1.html nonlinear2.html nonlinear3.html parallel.html param.html piecewise.html pixmap.html pm3dcolors.html pm3d.html pm3dgamma.html pm3d_clip.html pm3d_lighting.html pointsize.html polar.html poldat.html polar_quadrants.html polygons.html prob2.html prob.html projection.html rainbow.html random.html rectangle.html rgba_lines.html rgbalpha.html rgb_variable.html rotate_labels.html rugplot.html running_avg.html sampling.html scatter.html simple.html singulr.html smooth.html smooth_path.html solar_path.html spiderplot.html spline.html smooth_splines.html steps.html stringvar.html surface1.html surface2.html azimuth.html transparent.html transparent_solids.html textbox.html tics.html timedat.html ttics.html using.html varcolor.html vector.html violinplot.html walls.html world.html world2.html heatmaps.html stats.html unicode.html zerror.html boxes3d.html voxel.html vplot.html isosurface.html".Split(' ', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
                Webify(files, null, gnuplotPath, demoPath, htmlPath);
            }
            else if (args.Length == 4)
            {
                throw new NotImplementedException();
            }
            else
            {
                Console.WriteLine("Invalid number of parameters!");
            }
        }

        private static void Webify(string[] files, string? terminal, string gnuplotPath, string demoPath, string htmlPath)
        {
            foreach (var file in files)
            {
                if (file.Equals("index.html", StringComparison.Ordinal))
                {
                    continue;
                }

                Webify(file, terminal, gnuplotPath, demoPath, htmlPath);
            }
        }

        private static void Webify(string outFileName, string? terminal, string gnuplotPath, string demoPath, string htmlPath)
        {
            int plot = 1;

            // input and output files
            string fileName = Path.GetFileNameWithoutExtension(outFileName);
            var inFileName = fileName + ".dem";
            using var htmlStream = new StreamWriter(Path.Combine(htmlPath, outFileName));
            using var errStream = new StreamWriter(Path.Combine(htmlPath, fileName + ".err"));
            using var outStream = new StreamWriter(Path.Combine(htmlPath, fileName + ".out"));

            // open pipe to gnuplot and set terminal type
            var gnuplotProcess = new Process();
            gnuplotProcess.StartInfo.FileName = gnuplotPath;
            gnuplotProcess.StartInfo.UseShellExecute = false;
            gnuplotProcess.StartInfo.RedirectStandardInput = true;
            gnuplotProcess.StartInfo.RedirectStandardError = true;
            gnuplotProcess.StartInfo.RedirectStandardOutput = true;
            gnuplotProcess.OutputDataReceived += new DataReceivedEventHandler((sender, e) =>
            {
                outStream.Write(e.Data);
            });
            gnuplotProcess.ErrorDataReceived += new DataReceivedEventHandler((sender, e) =>
            {
                errStream.Write(e.Data);
            });
       
            if (!gnuplotProcess.Start())
            {
                Console.WriteLine("Failed to start gnuplot process.");
                Environment.Exit(-1);
            }

            var gnuplotInput = gnuplotProcess.StandardInput;
            gnuplotProcess.BeginOutputReadLine();
            gnuplotProcess.BeginErrorReadLine();

            gnuplotInput.WriteLine($"cd '{demoPath}'");
            Thread.Sleep(10000);

            //var gnuplotInput = new StreamWriter(Path.Combine(htmlPath, fileName + ".plt"));/*
            if (terminal is not null)
            {
                gnuplotInput.WriteLine($"set term {terminal}");
            }
            else
            {
                gnuplotInput.WriteLine("set term pngcairo font 'arial,10' transparent size 600,400");
            }

            gnuplotInput.WriteLine($"set output '{Path.Combine(htmlPath,$"{fileName}.{plot}.png")}'");

            // suppress animations
            gnuplotInput.WriteLine("NO_ANIMATION = 1");

//			# find out if gpsavediff is available in current path
//				my $savescripts = T;
//				{local $^W=0; $savescripts = open(FOO, "|gpsavediff") }
//				close FOO if ($savescripts);

            // Boiler plate header
            htmlStream.WriteLine("<html>");
            htmlStream.WriteLine("<head>");
            htmlStream.WriteLine($"<title>gnuplot demo script: {fileName}.dem </title>");
            htmlStream.WriteLine("<meta charset=\"UTF-8\" />");
            htmlStream.WriteLine("<link rel=\"stylesheet\" href=\"gnuplot_demo.css\" type=\"text/css\">");
            htmlStream.WriteLine("</head>");
            htmlStream.WriteLine("<body>");
            htmlStream.WriteLine("<a href=index.html><image src=return.png alt=\"Back to demo index\" class=\"icon-image\"></a>");
            htmlStream.WriteLine($"<h2>gnuplot demo script: <font color=blue>{fileName}.dem</font> </h2>\n");
            htmlStream.WriteLine($"<i>autogenerated by webify.pl on {DateTime.Now}</i>");

//			# try to find gnuplot version
//				$version = `$gnuplot --version`;
//				print OUT "\n<br><i>gnuplot version $version</i>";
				

            htmlStream.WriteLine("<hr>");

            // Start processing
            htmlStream.WriteLine($"<table><tr><td class='LIMG'><img src=\"{fileName}.{plot}.png\" alt=\"\">");
            htmlStream.WriteLine("</td><td><pre>");
            htmlStream.WriteLine();

            using var inStream = new StreamReader(Path.Combine(demoPath, inFileName));

            var line = inStream.ReadLine();
            while (line is not null)
            {
                if (Regex.IsMatch(line, "^ *pause -1"))
                {
//											if ($savescripts) {
//												print OUT "<br><p>Click <a href=$ARGV[0].$plot.gnu>here</a> ",
//												  "for minimal script to generate this plot</p>\n";
//												print GNUPLOT "save \"| gpsavediff > $ARGV[0].$plot.gnu\"\n";
//											}
                    htmlStream.WriteLine("</pre></td></tr></table>");
                    htmlStream.WriteLine("<br clear=all>");
                    htmlStream.WriteLine("<hr>");
                    plot++;
                    htmlStream.WriteLine("<table><tr><td class='LIMG'><img src=\"$ARGV[0].$plot.png\" alt=\"\">");
                    htmlStream.WriteLine("</td><td>");
                    htmlStream.WriteLine("<pre>");
                    gnuplotInput.WriteLine($"set output '{Path.Combine(htmlPath, $"{fileName}.{plot}.png")}'");
                }
                else if (Regex.IsMatch(line, "^pause"))
                {
                    gnuplotInput.WriteLine($"set output '{Path.Combine(htmlPath, $"{fileName}.{plot}.png")}'");
                }
                else if (Regex.IsMatch(line, "^ *reset"))
                {
                    gnuplotInput.WriteLine(line);
                }
                else
                {
                    htmlStream.WriteLine(HttpUtility.HtmlEncode(line));
                    gnuplotInput.WriteLine(line);
                }

                line = inStream.ReadLine();
            }

            // Amazingly enough, that's it.
            // Unlink leftover empty plot before leaving.
            gnuplotInput.WriteLine("set output");/**/
            gnuplotInput.WriteLine("exit");
            gnuplotProcess.WaitForExit(5000);
            htmlStream.WriteLine("</pre></td></tr></table>");
            htmlStream.WriteLine("</body>");
            htmlStream.WriteLine("</html>");
        }
    }
}
