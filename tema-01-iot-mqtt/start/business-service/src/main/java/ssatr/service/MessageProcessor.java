package ssatr.service;

import java.util.concurrent.BlockingQueue;

/**
 * Preia mesajele de un anumit tip ("telemetry", "alarm" sau "status") din coada
 * inbox, le salvează și actualizează dashboard-ul.
 *
 * Formatul mesajelor (câmpuri separate prin ";"):
 *   telemetry:  deviceId;timestamp;temperatura
 *   alarm:      deviceId;timestamp;temperatura;prag
 *   status:     deviceId;timestamp;text
 */
public class MessageProcessor implements Runnable {

    private final String kind;
    private final BlockingQueue<String> inbox;
    private final CsvStorage storage;
    private final Dashboard dashboard;

    public MessageProcessor(String kind, BlockingQueue<String> inbox, CsvStorage storage, Dashboard dashboard) {
        this.kind = kind;
        this.inbox = inbox;
        this.storage = storage;
        this.dashboard = dashboard;
    }

    @Override
    public void run() {
        // TODO 4: într-o buclă:
        //   - preluați un mesaj: inbox.take()
        //   - salvați mesajul în fișierul CSV: storage.append(kind, mesaj)
        //     (CsvStorage este gata implementată)
        //   - actualizați dashboard-ul, în funcție de kind
        //   - alarmele și confirmările se afișează imediat în consolă
        //   - un mesaj greșit (format invalid) nu trebuie să oprească thread-ul

            try {
                while (!Thread.currentThread().isInterrupted()) {
                    String message = inbox.take();
                    try {
                        storage.append(kind, message);

                        switch (kind) {
                            case "telemetry" -> {
                                String[] p = message.split(";");
                                dashboard.onTelemetry(p[0], Long.parseLong(p[1]), Double.parseDouble(p[2]));
                            }
                            case "alarm" -> {
                                String[] p = message.split(";");
                                dashboard.onAlarm(p[0]);
                                System.out.println("!!! ALARMA " + p[0] + ": temperatura " + p[2]
                                        + " depășește pragul " + p[3]);
                            }
                            case "status" -> {
                                String[] p = message.split(";", 3);
                                System.out.println("[status] " + p[0] + ": " + p[2]);
                            }
                            default -> System.err.println("Tip de mesaj necunoscut: " + kind);
                        }
                    } catch (Exception e) {
                        System.err.println("Mesaj invalid [" + kind + "] " + message
                                + " (" + e.getMessage() + ")");
                    }
                }
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        }
    }


