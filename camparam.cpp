#include <windows.h>
#include <dshow.h>

struct KeyValuePair
{
    const char *key;
    int value;
    enum Flags {
        flag_set,           // Set value from the command line
        flag_auto,          // Set auto but keep the value
        flag_increase,      // Increase current value by the value of argument
        flag_decrease       // Decrease current value by the value of argument
    } flags;
};

const int cameraControlPropertyCount = 7;
KeyValuePair cameraControlProperties[] = {
    {(char *)"pan", CameraControl_Pan},
    {(char *)"tilt", CameraControl_Tilt},
    {(char *)"roll", CameraControl_Roll},
    {(char *)"zoom", CameraControl_Zoom},
    {(char *)"exposure", CameraControl_Exposure},
    {(char *)"iris", CameraControl_Iris},
    {(char *)"focus", CameraControl_Focus}};

const int videoProcAmpPropertyCount = 10;
KeyValuePair videoProcAmpProperties[] = {
    {(char *)"brightness", VideoProcAmp_Brightness},
    {(char *)"contrast", VideoProcAmp_Contrast},
    {(char *)"hue", VideoProcAmp_Hue},
    {(char *)"saturation", VideoProcAmp_Saturation},
    {(char *)"sharpness", VideoProcAmp_Sharpness},
    {(char *)"gamma", VideoProcAmp_Gamma},
    {(char *)"colorenable", VideoProcAmp_ColorEnable},
    {(char *)"whitebalance", VideoProcAmp_WhiteBalance},
    {(char *)"backlightcompensation", VideoProcAmp_BacklightCompensation},
    {(char *)"gain", VideoProcAmp_Gain}};

const int MAX_COMMANDS = 18;
int g_commandCount = 0;
KeyValuePair g_commands[MAX_COMMANDS] = {};

void exitWithMessage(int code, const char *message)
{
    fprintf(stderr, "%s", message);
    exit(code);
}

void processArguments(int argc, char *argv[])
{
    for (int i = 1; i < argc; i++)
    {
        char *name = argv[i];
        int value = 0;
        KeyValuePair::Flags change_flags = KeyValuePair::flag_set;


        if (++i < argc)
         {
            if (_stricmp(argv[i], "auto") == 0)
            {
                change_flags = KeyValuePair::flag_auto;
            }
            else if (argv[i][0] == '+')
            {
                change_flags = KeyValuePair::flag_increase;
                value = abs(atoi(argv[i]));
            }
            else if (argv[i][0] == '-')
            {
                change_flags = KeyValuePair::flag_decrease;
                value = abs(atoi(argv[i]));
            }
            else
            {
                value = atoi(argv[i]);
            }
        }
        else
            exitWithMessage(1, "Invalid arguments.");

        g_commands[g_commandCount].key = name;
        g_commands[g_commandCount].value = value;
        g_commands[g_commandCount].flags = change_flags;

        g_commandCount++;

        if (g_commandCount > MAX_COMMANDS)
            exitWithMessage(1, "Too many arguments.");
    }
}

KeyValuePair *getKeyValuePairByKey(KeyValuePair keyValuePairs[], int count,
                                   const char *key)
{
    KeyValuePair *keyValuePair = NULL;

    for (int i = 0; i < count; i++)
    {
        if (_stricmp(key, keyValuePairs[i].key) == 0)
        {
            keyValuePair = &keyValuePairs[i];
        }
    }

    return keyValuePair;
}

int main(int argc, char *argv[])
{
    // Proccess arguments.
    processArguments(argc, argv);

    // Result handler.
    HRESULT resultHandle;

    // Intialize COM.
    resultHandle = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (resultHandle != S_OK)
        exitWithMessage(resultHandle, "Could not initialize COM.");

    // Create system device enumerator
    ICreateDevEnum *createDevEnum = NULL;
    resultHandle = CoCreateInstance(CLSID_SystemDeviceEnum, NULL,
                                    CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&createDevEnum));
    if (resultHandle != S_OK)
        exitWithMessage(resultHandle, "Could not create system device enumerator.");

    // Video input device enumerator
    IEnumMoniker *enumMoniker = NULL;
    resultHandle = createDevEnum->CreateClassEnumerator(
        CLSID_VideoInputDeviceCategory, &enumMoniker, 0);
    if (resultHandle != S_OK)
        exitWithMessage(resultHandle, "No video devices found.");

    // List all devices if no other commands are given.
    IMoniker *moniker;
    IPropertyBag *propertyBag;
    int deviceCount = 0;
    while (S_OK == enumMoniker->Next(1, &moniker, NULL))
    {
        resultHandle = moniker->BindToStorage(0, 0, IID_PPV_ARGS(&propertyBag));

        VARIANT variant;
        VariantInit(&variant);
        resultHandle = propertyBag->Read(L"FriendlyName", &variant, 0);

        // Print device name if other command were given.
        if (g_commandCount == 0)
            fprintf(stderr, "%d %ls\n", deviceCount, variant.bstrVal);

        VariantClear(&variant);

        deviceCount++;
    }

    // Exit if no commands were given.
    if (g_commandCount == 0)
        exit(0);

    // Get device.
    int deviceIndex;

    KeyValuePair *keyValuePair = getKeyValuePairByKey(g_commands, g_commandCount, (char *)"device");
    if (keyValuePair != NULL)
    {
        deviceIndex = keyValuePair->value;
    }
    else
    {
        exitWithMessage(1, "No device specified.");
    }

    // Exit if device index is invalid.
    if (deviceIndex >= deviceCount)
        exitWithMessage(1, "Invalid device index.");

    // Get device from collection.
    resultHandle = enumMoniker->Reset();
    resultHandle = enumMoniker->Skip(deviceIndex);
    resultHandle = enumMoniker->Next(1, &moniker, NULL);
    if (resultHandle != S_OK)
        exitWithMessage(resultHandle, "Could not get specified device.");

    // Create filter graph.
    IFilterGraph2 *filterGraph;
    resultHandle = CoCreateInstance(CLSID_FilterGraph, NULL,
                                    CLSCTX_INPROC_SERVER, IID_IGraphBuilder,
                                    (void **)&filterGraph);
    if (resultHandle != S_OK)
        exitWithMessage(resultHandle, "Could not create filter graph.");

    // Add device to graph.
    IBaseFilter *baseFilter;
    resultHandle = filterGraph->AddSourceFilterForMoniker(moniker, NULL, L"Source Filter", &baseFilter);
    if (resultHandle != S_OK)
        exitWithMessage(resultHandle, "Could not add source filter to graph.");

    // Get control interfaces.
    IAMCameraControl *cameraControl;
    resultHandle = baseFilter->QueryInterface(IID_IAMCameraControl, (void **)&cameraControl);
    if (resultHandle != S_OK)
        exitWithMessage(resultHandle, "Could not get camera control interface.");

    IAMVideoProcAmp *videoProcAmp;
    resultHandle = baseFilter->QueryInterface(IID_IAMVideoProcAmp, (void **)&videoProcAmp);
    if (resultHandle != S_OK)
        exitWithMessage(resultHandle, "Could not get video proc amp interface.");

    // Loop through commands.
    int updatedProperties = 0;
    for (int i = 0; i < g_commandCount; i++)
    {
        KeyValuePair command = g_commands[i];

        // Skip the device command.
        if (_stricmp(command.key, "device") == 0)
            continue;

        // Process video proc commands.
        KeyValuePair *videoProcAmpProperty = getKeyValuePairByKey(videoProcAmpProperties,
                                                                  videoProcAmpPropertyCount,
                                                                  g_commands[i].key);
        if (videoProcAmpProperty != NULL)
        {
            switch(g_commands[i].flags)
            {
                case KeyValuePair::flag_set:
                    {
                        videoProcAmp->Set(videoProcAmpProperty->value, command.value, VideoProcAmp_Flags_Manual);
                        fprintf(stdout, "%s %d\n", command.key, command.value);
                        break;
                    }

                case KeyValuePair::flag_auto:
                    {
                        long value, flags = 0;
                        videoProcAmp->Get(videoProcAmpProperty->value, &value, &flags);
                        videoProcAmp->Set(videoProcAmpProperty->value, value,
                                          VideoProcAmp_Flags_Auto);
                        fprintf(stdout, "Auto for %s is on, keep value %ld\n", command.key, value);
                        break;
                    }

                case KeyValuePair::flag_increase:
                    {
                        long value, flags = 0;
                        videoProcAmp->Get(videoProcAmpProperty->value, &value, &flags);

                        value += g_commands[i].value;
                        videoProcAmp->Set(videoProcAmpProperty->value,
                                          value,
                                          VideoProcAmp_Flags_Manual);

                        // Get the value that was actually set
                        videoProcAmp->Get(videoProcAmpProperty->value, &value, &flags);
                        fprintf(stdout, "Increase value for %s by %d, new value %ld\n",
                                command.key, g_commands[i].value, value);
                        break;
                    }

                case KeyValuePair::flag_decrease:
                    {
                        long value, flags = 0;
                        videoProcAmp->Get(videoProcAmpProperty->value, &value, &flags);

                        value -= g_commands[i].value;
                        videoProcAmp->Set(videoProcAmpProperty->value,
                                          value,
                                          VideoProcAmp_Flags_Manual);

                        // Get the value that was actually set
                        fprintf(stdout, "Decrease value for %s by %d, new value %ld\n",
                                command.key, g_commands[i].value, value);
                        break;
                    }
            }
            updatedProperties++;
            continue;
        }

        // Process camera control commands.
        KeyValuePair *cameraControlProperty = getKeyValuePairByKey(cameraControlProperties,
                                                                   cameraControlPropertyCount,
                                                                   g_commands[i].key);
        if (cameraControlProperty != NULL)
        {
            cameraControl->Set(cameraControlProperty->value, command.value, CameraControl_Flags_Manual);
            fprintf(stdout, "%s %d\n", command.key, command.value);
            updatedProperties++;
            continue;
        }

        fprintf(stdout, "Unknown argument: %s\n", command.key);
    }

    if (updatedProperties == 0)
    {
        // Print camera control properties.
        for (int i = 0; i < cameraControlPropertyCount; i++)
        {
            KeyValuePair property = cameraControlProperties[i];
            long value, flags = 0;
            resultHandle = cameraControl->Get(property.value, &value, &flags);
            if (resultHandle == S_OK)
                fprintf(stdout, "%s %ld\n", property.key, value);
        }

        // Print video proc amp properties.
        for (int i = 0; i < videoProcAmpPropertyCount; i++)
        {
            KeyValuePair property = videoProcAmpProperties[i];
            long value, flags = 0;
            resultHandle = videoProcAmp->Get(property.value, &value, &flags);
            const char *flagStr = "[Unknown]";
            if (flags == VideoProcAmp_Flags_Manual)
                flagStr = "";
            else if (flags == VideoProcAmp_Flags_Auto)
                flagStr = "[A]";
            if (resultHandle == S_OK)
                fprintf(stdout, "%s %ld %s\n", property.key, value, flagStr);
        }
    }

    return (0);
}
