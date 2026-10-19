package ssatr.service;

import java.util.HashMap;
import java.util.Locale;
import java.util.Map;
import java.util.TreeMap;

/**
 * Ultima stare cunoscută a fiecărui device: ultima temperatură, momentul
 * ultimului mesaj, numărul de alarme etc.
 *
 * Este actualizată de thread-urile MessageProcessor și citită de CommandConsole,
 * deci accesul trebuie sincronizat.
 */
public class Dashboard {

    // TODO 5: alegeți structura de date pentru starea device-urilor
    //   (de exemplu un Map<String, ...> cu cheia deviceId)
    //   și adăugați metodele de actualizare apelate din MessageProcessor.

    private static class DeviceState {
        double lastTemperature;
        long lastMessageTime;
        int alarmCount;
    }

    private final Map<String, DeviceState> devices = new HashMap<>();

    public void print() {
        // TODO 5: afișați câte un rând pentru fiecare device, de exemplu:
        //   DEVICE     TEMP    ULTIMUL MESAJ   ALARME
        //   device-1   23.41   acum 1 s        2
        if (devices.isEmpty()) {
            System.out.println("(niciun device nu a trimis mesaje încă)");
            return;
        }

        System.out.printf("%-12s %8s   %-14s %s%n", "DEVICE", "TEMP", "ULTIMUL MESAJ", "ALARME");

        long now = System.currentTimeMillis();
        for (Map.Entry<String, DeviceState> e : devices.entrySet()) {
            DeviceState s = e.getValue();
            long ageSeconds = (now - s.lastMessageTime) / 1000;
            System.out.printf(Locale.US, "%-12s %8.2f   %-14s %d%n",
                    e.getKey(), s.lastTemperature, "acum " + ageSeconds + " s", s.alarmCount);
        }
    }

    public synchronized void onTelemetry(String deviceId, long timestamp, double temperature) {
        DeviceState s = devices.computeIfAbsent(deviceId, id -> new DeviceState());
        s.lastTemperature = temperature;
        s.lastMessageTime = timestamp;
    }

    public synchronized void onAlarm(String deviceId) {
        devices.computeIfAbsent(deviceId, id -> new DeviceState()).alarmCount++;
    }

}
