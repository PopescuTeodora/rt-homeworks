package ssatr.device;

import java.util.Locale;
import java.util.concurrent.BlockingQueue;

/**
 * TASK SPORADIC: tratează alarmele de temperatură.
 *
 * Evenimentele sosesc în coada alarmEvents (puse de taskul periodic).
 * Intervalul minim între sosiri este MIN_INTERARRIVAL_MS: între două eliberări
 * ale taskului (două alarme publicate) trebuie să treacă cel puțin atât.
 * Intervalul este impus de cod: evenimentele care sosesc mai des sunt
 * respinse, ca să nu fie inundat brokerul.
 */
public class SporadicAlarmTask implements Runnable {

    static final long MIN_INTERARRIVAL_MS = 5000;

    private final String deviceId;
    private final MqttConnection mqtt;
    private final BlockingQueue<Double> alarmEvents;
    private final PeriodicTelemetryTask periodic;


    public SporadicAlarmTask(String deviceId, MqttConnection mqtt, BlockingQueue<Double> alarmEvents, PeriodicTelemetryTask periodic) {
        this.deviceId = deviceId;
        this.mqtt = mqtt;
        this.alarmEvents = alarmEvents;
        this.periodic = periodic;
    }

    @Override
    public void run() {
        // TODO 2: implementați taskul sporadic:
        //   - așteptați un eveniment: alarmEvents.take()
        //   - dacă de la ultima eliberare au trecut mai puțin de
        //     MIN_INTERARRIVAL_MS, respingeți evenimentul (afișați un mesaj)
        //   - altfel publicați pe Config.alarmTopic(deviceId) mesajul
        //     "deviceId;timestamp;temperatura;prag"
        //     (pragul curent poate fi transmis împreună cu temperatura sau
        //      citit din PeriodicTelemetryTask; alegeți și justificați în raport)
        //
        // Vezi exemplul exemple/01-taskuri/.../SporadicTaskExample.java

        long lastRelease = 0;
        try {
            while (!Thread.currentThread().isInterrupted()) {
                double t = alarmEvents.take();
                long now = System.currentTimeMillis();

                long sinceLast = now - lastRelease;
                if (now - lastRelease < MIN_INTERARRIVAL_MS) {
                    System.out.println("Alarmă RESPINSA: au trecut doar " + sinceLast
                            + " ms (minim " + MIN_INTERARRIVAL_MS + " ms)");
                    continue;
                }
                lastRelease = now;

                String msg = deviceId + ";" + now + ";"
                        + String.format(Locale.US, "%.2f", t) + ";"
                        + String.format(Locale.US, "%.2f", periodic.getThreshold());
                mqtt.publish(Config.alarmTopic(deviceId), msg);
                System.out.println("Alarmă PUBLICATA: " + msg);
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }

}
