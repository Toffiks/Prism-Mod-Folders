using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.IO.Compression;
using System.Globalization;
using System.Linq;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Windows.Forms;

internal static class UiText
{
    internal static bool Russian =
        String.Equals(CultureInfo.CurrentUICulture.TwoLetterISOLanguageName, "ru", StringComparison.OrdinalIgnoreCase);

    internal static string Get(string russian, string english)
    {
        return Russian ? russian : english;
    }
}

internal sealed class PatchDefinition
{
    internal string Version;
    internal string OfficialSha256;
    internal string PatchedSha256;
    internal string DeltaResource;
}

internal static class PatchCatalog
{
    internal static readonly PatchDefinition[] All =
    {
        new PatchDefinition
        {
            Version = "11.0.3",
            OfficialSha256 = "C24C7C84FCE7FF1D12C709E0BCC8993AAA2A8CB662381C960CCB7D93C88BC2E3",
            PatchedSha256 = "E91DDEB27A1679F91F2FB10DC391CFC13EFF76DA7D0FF115C77C48D3274128A0",
            DeltaResource = "PrismModFolders.Delta.11.0.3"
        }
    };

    internal static PatchDefinition FindOfficial(string hash)
    {
        return All.FirstOrDefault(item => String.Equals(item.OfficialSha256, hash, StringComparison.OrdinalIgnoreCase));
    }

    internal static PatchDefinition FindPatched(string hash)
    {
        return All.FirstOrDefault(item => String.Equals(item.PatchedSha256, hash, StringComparison.OrdinalIgnoreCase));
    }

    internal static PatchDefinition FindVersion(string version)
    {
        return All.FirstOrDefault(item => String.Equals(item.Version, version, StringComparison.OrdinalIgnoreCase));
    }

    internal static Stream OpenDelta(PatchDefinition definition)
    {
        Stream stream = Assembly.GetExecutingAssembly().GetManifestResourceStream(definition.DeltaResource);
        if (stream == null)
        {
            throw new InvalidOperationException(UiText.Get(
                "В патчере отсутствует delta для Prism Launcher " + definition.Version + ".",
                "The patcher does not contain a delta for Prism Launcher " + definition.Version + "."));
        }
        return stream;
    }

    internal static string SupportedVersions
    {
        get { return String.Join(", ", All.Select(item => item.Version).ToArray()); }
    }
}

internal static class PatchEngine
{
    internal static void ApplyDelta(string sourceFile, Stream delta, string outputFile)
    {
        using (FileStream source = new FileStream(sourceFile, FileMode.Open, FileAccess.Read, FileShare.Read))
        using (BinaryReader header = new BinaryReader(delta, Encoding.UTF8, true))
        using (FileStream output = new FileStream(outputFile, FileMode.Create, FileAccess.Write, FileShare.None))
        {
            byte[] magic = header.ReadBytes(8);
            if (magic.Length != 8 || magic[0] != 'P' || magic[1] != 'M' || magic[2] != 'F' || magic[3] != 'D' ||
                magic[4] != 'L' || magic[5] != 'T' || magic[6] != '1' || magic[7] != 0)
            {
                throw new InvalidDataException(UiText.Get(
                    "Неверный заголовок встроенного delta-патча.",
                    "The embedded delta patch has an invalid header."));
            }

            long expectedLength = header.ReadInt64();
            int commandLength = header.ReadInt32();
            if (expectedLength <= 0 || expectedLength > 256L * 1024L * 1024L || commandLength <= 0)
            {
                throw new InvalidDataException(UiText.Get(
                    "Некорректные размеры во встроенном delta-патче.",
                    "The embedded delta patch contains invalid sizes."));
            }

            using (DeflateStream compressed = new DeflateStream(delta, CompressionMode.Decompress, true))
            using (BinaryReader commands = new BinaryReader(compressed, Encoding.UTF8, true))
            {
                int consumed = 0;
                bool ended = false;
                byte[] buffer = new byte[1024 * 1024];

                while (consumed < commandLength)
                {
                    byte command = commands.ReadByte();
                    ++consumed;
                    if (command == 0)
                    {
                        ended = true;
                        break;
                    }

                    if (command == 1)
                    {
                        int offset = commands.ReadInt32();
                        int count = commands.ReadInt32();
                        consumed = CheckedAdd(consumed, 8);
                        if (offset < 0 || count <= 0 || (long)offset + count > source.Length)
                        {
                            throw new InvalidDataException(UiText.Get(
                                "Delta-патч ссылается за пределы исходного файла.",
                                "The delta patch references data outside the source file."));
                        }
                        source.Position = offset;
                        CopyExact(source, output, count, buffer);
                    }
                    else if (command == 2)
                    {
                        int count = commands.ReadInt32();
                        if (count <= 0)
                        {
                            throw new InvalidDataException(UiText.Get(
                                "Некорректная literal-команда delta-патча.",
                                "The delta patch contains an invalid literal command."));
                        }
                        consumed = CheckedAdd(consumed, CheckedAdd(4, count));
                        CopyExact(commands.BaseStream, output, count, buffer);
                    }
                    else
                    {
                        throw new InvalidDataException(UiText.Get(
                            "Неизвестная команда delta-патча.",
                            "The delta patch contains an unknown command."));
                    }

                    if (consumed > commandLength || output.Length > expectedLength)
                    {
                        throw new InvalidDataException(UiText.Get(
                            "Повреждённый delta-патч.",
                            "The delta patch is corrupted."));
                    }
                }

                if (!ended || consumed != commandLength)
                {
                    throw new InvalidDataException(UiText.Get(
                        "Delta-патч закончился неожиданно.",
                        "The delta patch ended unexpectedly."));
                }
            }

            output.Flush(true);
            if (output.Length != expectedLength)
            {
                throw new InvalidDataException(UiText.Get(
                    "Итоговый файл имеет неожиданный размер.",
                    "The output file has an unexpected size."));
            }
        }
    }

    private static int CheckedAdd(int left, int right)
    {
        return checked(left + right);
    }

    private static void CopyExact(Stream input, Stream output, int count, byte[] buffer)
    {
        while (count > 0)
        {
            int requested = Math.Min(count, buffer.Length);
            int read = input.Read(buffer, 0, requested);
            if (read <= 0)
            {
                throw new EndOfStreamException(UiText.Get(
                    "Delta-патч оборвался раньше времени.",
                    "The delta patch ended before all requested data was read."));
            }
            output.Write(buffer, 0, read);
            count -= read;
        }
    }
}

internal static class Program
{
    [STAThread]
    private static void Main(string[] args)
    {
        if (args.Length == 3 && String.Equals(args[0], "--self-test", StringComparison.OrdinalIgnoreCase))
        {
            RunSelfTest(args[1], args[2]);
            return;
        }

        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        Application.Run(new PatcherForm());
    }

    private static void RunSelfTest(string officialFile, string outputFile)
    {
        try
        {
            string sourceHash = FileHash.Sha256(officialFile);
            PatchDefinition definition = PatchCatalog.FindOfficial(sourceHash);
            if (definition == null)
            {
                throw new InvalidDataException(UiText.Get(
                    "Self-test: неизвестный исходный SHA-256 " + sourceHash,
                    "Self-test: unknown source SHA-256 " + sourceHash));
            }
            using (Stream delta = PatchCatalog.OpenDelta(definition))
            {
                PatchEngine.ApplyDelta(officialFile, delta, outputFile);
            }
            string outputHash = FileHash.Sha256(outputFile);
            if (!String.Equals(outputHash, definition.PatchedSha256, StringComparison.OrdinalIgnoreCase))
            {
                throw new InvalidDataException(UiText.Get(
                    "Self-test: неверный итоговый SHA-256 " + outputHash,
                    "Self-test: incorrect output SHA-256 " + outputHash));
            }
            Environment.ExitCode = 0;
        }
        catch
        {
            Environment.ExitCode = 1;
            throw;
        }
    }
}

internal sealed class PatcherForm : Form
{
    private readonly TextBox _path = new TextBox();
    private readonly Label _description = new Label();
    private readonly Label _pathLabel = new Label();
    private readonly Label _status = new Label();
    private readonly Button _browse = new Button();
    private readonly Button _install = new Button();
    private readonly Button _restore = new Button();
    private readonly ComboBox _language = new ComboBox();

    private static string StateDirectory
    {
        get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "PrismModFolders"); }
    }

    private static string StateFile
    {
        get { return Path.Combine(StateDirectory, "state.txt"); }
    }

    internal PatcherForm()
    {
        ClientSize = new Size(650, 235);
        MinimumSize = new Size(620, 270);
        StartPosition = FormStartPosition.CenterScreen;
        Font = new Font("Segoe UI", 9F);

        _description.AutoSize = false;
        _description.Location = new Point(16, 14);
        _description.Size = new Size(500, 52);

        _language.DropDownStyle = ComboBoxStyle.DropDownList;
        _language.Location = new Point(530, 14);
        _language.Size = new Size(102, 23);
        _language.Items.Add("Русский");
        _language.Items.Add("English");
        _language.SelectedIndex = UiText.Russian ? 0 : 1;
        _language.SelectedIndexChanged += ChangeLanguage;

        _pathLabel.AutoSize = true;
        _pathLabel.Location = new Point(16, 76);

        _path.Location = new Point(16, 98);
        _path.Size = new Size(535, 23);
        _path.Text = DetectLauncher();

        _browse.Location = new Point(560, 96);
        _browse.Size = new Size(72, 27);
        _browse.Click += Browse;

        _install.Location = new Point(16, 139);
        _install.Size = new Size(190, 32);
        _install.Click += InstallPatch;

        _restore.Location = new Point(216, 139);
        _restore.Size = new Size(190, 32);
        _restore.Click += RestoreOriginal;

        _status.AutoSize = false;
        _status.Location = new Point(16, 184);
        _status.Size = new Size(615, 40);
        Controls.Add(_description);
        Controls.Add(_language);
        Controls.Add(_pathLabel);
        Controls.Add(_path);
        Controls.Add(_browse);
        Controls.Add(_install);
        Controls.Add(_restore);
        Controls.Add(_status);

        ApplyLanguage();
    }

    private void ChangeLanguage(object sender, EventArgs args)
    {
        UiText.Russian = _language.SelectedIndex == 0;
        ApplyLanguage();
    }

    private void ApplyLanguage()
    {
        Text = UiText.Get("Папки модов для Prism Launcher", "Mod Folders for Prism Launcher");
        _description.Text = UiText.Get(
            "Патчер изменяет только prismlauncher.exe и не открывает каталог сборок, аккаунтов или настроек. " +
                "Перед заменой оригинальный файл сохраняется для восстановления.",
            "The patcher changes only prismlauncher.exe and does not access your instances, accounts, or settings. " +
                "The original executable is backed up before replacement.");
        _pathLabel.Text = UiText.Get("Установленный prismlauncher.exe:", "Installed prismlauncher.exe:");
        _browse.Text = UiText.Get("Обзор…", "Browse…");
        _install.Text = UiText.Get("Установить папки модов", "Install Mod Folders");
        _restore.Text = UiText.Get("Восстановить оригинал", "Restore Original");
        _status.Text = UiText.Get(
            "Поддерживаемые официальные Windows-сборки Prism Launcher: ",
            "Supported official Prism Launcher Windows builds: ") + PatchCatalog.SupportedVersions + ".";
    }

    private static string DetectLauncher()
    {
        string local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        return Path.Combine(local, "Programs", "PrismLauncher", "prismlauncher.exe");
    }

    private void Browse(object sender, EventArgs args)
    {
        using (OpenFileDialog dialog = new OpenFileDialog())
        {
            dialog.Title = UiText.Get("Выберите prismlauncher.exe", "Select prismlauncher.exe");
            dialog.Filter = UiText.Get(
                "Prism Launcher|prismlauncher.exe|Исполняемые файлы|*.exe",
                "Prism Launcher|prismlauncher.exe|Executable files|*.exe");
            dialog.CheckFileExists = true;
            if (dialog.ShowDialog(this) == DialogResult.OK)
            {
                _path.Text = dialog.FileName;
            }
        }
    }

    private void InstallPatch(object sender, EventArgs args)
    {
        RunOperation(delegate
        {
            string target = ValidateTarget();
            EnsureLauncherClosed();

            string currentHash = FileHash.Sha256(target);
            PatchDefinition alreadyPatched = PatchCatalog.FindPatched(currentHash);
            if (alreadyPatched != null)
            {
                return UiText.Get(
                    "Патч для Prism Launcher " + alreadyPatched.Version + " уже установлен.",
                    "The patch for Prism Launcher " + alreadyPatched.Version + " is already installed.");
            }

            PatchDefinition definition = PatchCatalog.FindOfficial(currentHash);
            if (definition == null)
            {
                throw new InvalidOperationException(UiText.Get(
                    "Этот prismlauncher.exe пока не поддерживается. Файл не изменён.\r\n\r\nSHA-256: " + currentHash +
                        "\r\nПоддерживаемые версии: " + PatchCatalog.SupportedVersions,
                    "This prismlauncher.exe is not supported yet. The file was not changed.\r\n\r\nSHA-256: " +
                        currentHash + "\r\nSupported versions: " + PatchCatalog.SupportedVersions));
            }

            Directory.CreateDirectory(StateDirectory);
            string backupDirectory = Path.Combine(StateDirectory, "backups", definition.Version, currentHash);
            Directory.CreateDirectory(backupDirectory);
            string backup = Path.Combine(backupDirectory, "prismlauncher.exe");
            if (!File.Exists(backup))
            {
                File.Copy(target, backup, false);
            }

            string staged = target + ".modfolders.new";
            try
            {
                using (Stream delta = PatchCatalog.OpenDelta(definition))
                {
                    PatchEngine.ApplyDelta(target, delta, staged);
                }
                if (!String.Equals(FileHash.Sha256(staged), definition.PatchedSha256, StringComparison.OrdinalIgnoreCase))
                {
                    throw new IOException(UiText.Get(
                        "Проверка собранного файла не прошла.",
                        "The patched file failed verification."));
                }

                File.Replace(staged, target, null, true);
                WriteState(target, backup, definition);
            }
            finally
            {
                if (File.Exists(staged))
                {
                    File.Delete(staged);
                }
            }
            return UiText.Get(
                "Готово. Папки модов установлены в Prism Launcher " + definition.Version +
                    "; сборки и настройки не изменялись.",
                "Done. Mod Folders were installed in Prism Launcher " + definition.Version +
                    "; instances and settings were not changed.");
        });
    }

    private void RestoreOriginal(object sender, EventArgs args)
    {
        RunOperation(delegate
        {
            EnsureLauncherClosed();
            PatchState state = ReadState();
            PatchDefinition definition = PatchCatalog.FindVersion(state.Version);
            if (definition == null)
            {
                throw new InvalidDataException(UiText.Get(
                    "Состояние патчера относится к неизвестной версии Prism Launcher.",
                    "The saved patch state refers to an unknown Prism Launcher version."));
            }
            if (!File.Exists(state.Target) ||
                !String.Equals(Path.GetFileName(state.Target), "prismlauncher.exe", StringComparison.OrdinalIgnoreCase))
            {
                throw new FileNotFoundException(UiText.Get(
                    "Установленный prismlauncher.exe не найден.",
                    "The installed prismlauncher.exe was not found."), state.Target);
            }
            if (!File.Exists(state.Backup))
            {
                throw new FileNotFoundException(UiText.Get(
                    "Резервная копия оригинального файла не найдена.",
                    "The backup of the original executable was not found."), state.Backup);
            }

            string currentHash = FileHash.Sha256(state.Target);
            if (!String.Equals(currentHash, state.PatchedSha256, StringComparison.OrdinalIgnoreCase))
            {
                throw new InvalidOperationException(UiText.Get(
                    "Текущий prismlauncher.exe изменился после установки патча. Восстановление остановлено, чтобы не затереть обновление.\r\n\r\nSHA-256: " +
                        currentHash,
                    "The current prismlauncher.exe changed after the patch was installed. Restore was stopped to avoid overwriting an update.\r\n\r\nSHA-256: " +
                        currentHash));
            }
            string backupHash = FileHash.Sha256(state.Backup);
            if (!String.Equals(backupHash, definition.OfficialSha256, StringComparison.OrdinalIgnoreCase))
            {
                throw new InvalidDataException(UiText.Get(
                    "Контрольная сумма резервной копии не совпадает с официальным Prism Launcher.",
                    "The backup checksum does not match the official Prism Launcher executable."));
            }

            string staged = state.Target + ".modfolders.restore";
            try
            {
                File.Copy(state.Backup, staged, true);
                File.Replace(staged, state.Target, null, true);
                if (File.Exists(StateFile))
                {
                    File.Delete(StateFile);
                }
            }
            finally
            {
                if (File.Exists(staged))
                {
                    File.Delete(staged);
                }
            }
            return UiText.Get(
                "Официальный prismlauncher.exe восстановлен. Данные лаунчера не изменялись.",
                "The official prismlauncher.exe was restored. Launcher data was not changed.");
        });
    }

    private void RunOperation(Func<string> operation)
    {
        SetBusy(true);
        try
        {
            _status.Text = operation();
            MessageBox.Show(this, _status.Text, "Prism Mod Folders", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }
        catch (Exception error)
        {
            _status.Text = UiText.Get("Ошибка: ", "Error: ") + error.Message;
            MessageBox.Show(this, error.Message, "Prism Mod Folders", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
        finally
        {
            SetBusy(false);
        }
    }

    private void SetBusy(bool busy)
    {
        _install.Enabled = !busy;
        _restore.Enabled = !busy;
        Cursor = busy ? Cursors.WaitCursor : Cursors.Default;
    }

    private string ValidateTarget()
    {
        string target = Path.GetFullPath(_path.Text.Trim());
        if (!File.Exists(target) ||
            !String.Equals(Path.GetFileName(target), "prismlauncher.exe", StringComparison.OrdinalIgnoreCase))
        {
            throw new FileNotFoundException(UiText.Get(
                "Выберите существующий prismlauncher.exe.",
                "Select an existing prismlauncher.exe."), target);
        }
        return target;
    }

    private static void EnsureLauncherClosed()
    {
        if (Process.GetProcessesByName("prismlauncher").Any())
        {
            throw new InvalidOperationException(UiText.Get(
                "Сначала полностью закройте Prism Launcher и повторите операцию.",
                "Close Prism Launcher completely, then try again."));
        }
    }

    private static void WriteState(string target, string backup, PatchDefinition definition)
    {
        string[] lines =
        {
            Convert.ToBase64String(Encoding.UTF8.GetBytes(target)),
            Convert.ToBase64String(Encoding.UTF8.GetBytes(backup)),
            definition.PatchedSha256,
            definition.Version
        };
        File.WriteAllLines(StateFile, lines, Encoding.UTF8);
    }

    private static PatchState ReadState()
    {
        if (!File.Exists(StateFile))
        {
            throw new FileNotFoundException(UiText.Get(
                "Сведения об установленном патче не найдены.",
                "No saved information about an installed patch was found."), StateFile);
        }
        string[] lines = File.ReadAllLines(StateFile);
        if (lines.Length < 3)
        {
            throw new InvalidDataException(UiText.Get(
                "Файл состояния патчера повреждён.",
                "The patch state file is corrupted."));
        }
        return new PatchState
        {
            Target = Encoding.UTF8.GetString(Convert.FromBase64String(lines[0])),
            Backup = Encoding.UTF8.GetString(Convert.FromBase64String(lines[1])),
            PatchedSha256 = lines[2],
            Version = lines.Length >= 4 ? lines[3] : ""
        };
    }

    private sealed class PatchState
    {
        internal string Target;
        internal string Backup;
        internal string PatchedSha256;
        internal string Version;
    }
}

internal static class FileHash
{
    internal static string Sha256(string file)
    {
        using (FileStream stream = new FileStream(file, FileMode.Open, FileAccess.Read, FileShare.Read))
        using (SHA256 sha = SHA256.Create())
        {
            return BitConverter.ToString(sha.ComputeHash(stream)).Replace("-", String.Empty);
        }
    }
}
